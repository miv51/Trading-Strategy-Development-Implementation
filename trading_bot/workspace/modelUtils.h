
/*
A predictive model that consists of the following ...
a minimalistic implementation of a MLP (feed foward neural network) that is used to predict transitions
the outlier conditions for all the input variables

model weights and min/max outlier values are saved as separate json files
this allows the model to be periodically retrianed without having to re-build the bot

include "model_weights.json" and "outlier_values.json" in the same root directory as the executable

The model only uses stack memory so the architecture cannot be changed - only the weights ...
... can be changed by retraining the model with tensorflow and saving the weights in the json ...
... format specified in the notebook used to make the model.
*/

#ifndef MODEL_UTILS_H
#define MODEL_UTILS_H

#include "exceptUtils.h"
#include "jsonUtils.h"

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>

#include <random>

inline float godSays() //see if the bot can manage orders and positions when it randomly decides to buy & sell stocks
{
    std::mt19937 generator(std::random_device{}());

    std::uniform_real_distribution<float> distribution(0.0, 1.0);

    return distribution(generator);
}

typedef std::vector<float> weightArray;
typedef std::vector<weightArray> layerWeightContainer;
typedef std::vector<layerWeightContainer> weightContainer;

//leaky relu implementation
template<size_t output_length>
inline void leaky_relu(float(&output)[output_length])
{
    for (int i = 0; i < output_length; i++)
    {
        if (output[i] < 0.0F) output[i] *= 0.1F;
    }
}

//softmax implementation
template<size_t output_length>
inline void softmax(float(&output)[output_length])
{
    float vector_sum = 0.0F;

    for (int i = 0; i < output_length; i++)
    {
        output[i] = std::exp(output[i]);
        vector_sum += output[i];
    }

    if (vector_sum != 0.0F && std::isfinite(vector_sum))
    {
        for (int i = 0; i < output_length; i++) output[i] /= vector_sum;
    }
    else
    {
        for (int i = 0; i < output_length; i++) output[i] = 0.0F;
    }
}

template<size_t input_length, size_t output_length, void(*activation_function)(float(&output)[output_length])>
class denseLayer
{
public:
    denseLayer()
    {
        for (int i = 0; i < output_length * input_length; i++) weights[i] = 0.0F;
        for (int i = 0; i < output_length; i++) biases[i] = 0.0F;
        for (int i = 0; i < output_length; i++) output[i] = 0.0F;
    }

    ~denseLayer() {}

    float output[output_length];

    inline void operator()(float(&input)[input_length])
    {
        //skipped check - input.size() == input_length should be true

        float sum;

        int input_index;

        for (int output_index = 0; output_index < output_length; output_index++)
        {
            sum = biases[output_index];

            //for (input_index = 0; input_index < input_length; input_index++)  += weights[output_index * input_length + input_index] * input[input_index];
            
            /*
            loop unrolling greatly reduces the number of assignment operations and loop overhead
            doing the following instead of the commented line above reduces computation time by 50%
            */

            for (input_index = 0; input_index < input_length - input_length % 8; input_index += 8)
            {
                sum += weights[output_index + input_index * output_length] * input[input_index] \
                    + weights[output_index + (input_index + 1) * output_length] * input[input_index + 1] \
                    + weights[output_index + (input_index + 2) * output_length] * input[input_index + 2] \
                    + weights[output_index + (input_index + 3) * output_length] * input[input_index + 3] \
                    + weights[output_index + (input_index + 4) * output_length] * input[input_index + 4] \
                    + weights[output_index + (input_index + 5) * output_length] * input[input_index + 5] \
                    + weights[output_index + (input_index + 6) * output_length] * input[input_index + 6] \
                    + weights[output_index + (input_index + 7) * output_length] * input[input_index + 7];
            }

            for (; input_index < input_length; input_index++)
            {
                sum += weights[output_index + input_index * output_length] * input[input_index];
            }

            output[output_index] = sum;
        }

        activation_function(output);
    }

    void setWeights(weightArray& weight_array, weightArray& bias_array)
    {
        if (weight_array.size() != input_length * output_length) throw std::runtime_error("Input and/or output dimensions do not match layer dimensions.");
        if (bias_array.size() != output_length) throw std::runtime_error("Output dimension does not match layer dimensions.");

        for (int i = 0; i < input_length * output_length; i++) { weights[i] = weight_array[i]; }
        for (int i = 0; i < output_length; i++) { biases[i] = bias_array[i]; }
    }

private:

    float weights[input_length * output_length];
    float biases[output_length];
};

template<size_t input_length, size_t output_length>
class simpleNeuralNet
{
public:
    simpleNeuralNet() { for (int i = 0; i < output_length; i++) output[i] = 0.0F; }
    ~simpleNeuralNet() {}

    float output[output_length];

    inline void operator()(float(&input)[input_length])
    {
        int i;

        layer0(input);
        layer1(layer0.output);
        layer2(layer1.output);
        layer3(layer2.output);

        for (i = 0; i < 16; i++) layer3.output[i] += layer1.output[i];

        layer4(layer3.output);
        layer5(layer4.output);

        for (i = 0; i < 16; i++) layer5.output[i] += layer3.output[i];

        layer6(layer5.output);

        for (i = 0; i < output_length; i++) output[i] = layer6.output[i];
    }

    denseLayer<input_length, 32, leaky_relu<32>> layer0;
    denseLayer<32, 16, leaky_relu<16>> layer1;
    denseLayer<16, 32, leaky_relu<32>> layer2;
    denseLayer<32, 16, leaky_relu<16>> layer3;
    denseLayer<16, 32, leaky_relu<32>> layer4;
    denseLayer<32, 16, leaky_relu<16>> layer5;
    denseLayer<16, output_length, softmax<output_length>> layer6;
};

struct inlierRanges //contains the min & maximum allowed values for each input parameter
{
    long long rolling_period = 2000000000LL; //rolling period in nanoseconds

    int rolling_period_min_trades = 5; //minimum rolling_csum
    int rolling_period_max_trades = 999999999; //maximum rolling_csum

    int rolling_volume_min = 0; //minimum rolling_vsum
    int rolling_volume_max = 999999999; //maximum rolling_vsum

    int lookback_period = 1024; //number of past daily closing prices included in the qpl calculations

    double std_max = 3.0; //maximum number of standard deviations that a closing price can be from the mean in order to be used for the calculation
    double number_of_bins = 51.0; //number of segments the probability distribution is divided into

    int min_completed_trading_days = 500; //minimum number of initial daily closing prices that are needed to perform the calculation
    int average_volume_period = 70; //number of days used to calculate the average volume

    double min_previous_days_closing_price = 0.0001;
    double max_previous_days_closing_price = 999999999.0;

    double min_average_volume = 1.0;
    double max_average_volume = 999999999.0;

    double min_mean = 0.0; //minimum allowed mean (not lognormal mean) of the relative returns
    double max_mean = 999999999.0; //maximum allowed mean (not lognormal mean) of the relative returns

    double min_std = 0.0; //minimum allowed standard deviation (not lognormal) of the relative returns
    double max_std = 999999999.0; //maximum allowed standard deviation (not lognormal) of the relative returns

    double min_lambda = 0.35; //minimum allowed lambda value (must be at least 0.35 to avoid math domain issues)
    double max_lambda = 999999999.0; //maximum allowed lambda value

    double min_dp = -999999999.0; //minimum allowed price change within the last rolling period
    double max_dp = +999999999.0; //maximum allowed price change within the last rolling period

    double min_dt = 0.0; //minimum allowed difference in time between the first and last trade that occured within the last rolling period
    double max_dt = 2.0; //maximum allowed difference in time between the first and last trade that occured within the last rolling period

    int min_n = -999999999;
    int max_n = +999999999;

    int min_vsum = 0; //minimum allowed cumulative volume sum over the current trading day
    int max_vsum = 999999999; //maximum allowed cumulative volume sum over the current trading day

    double min_rvol = 0.0;
    double max_rvol = 999999999.0;

    double min_time_of_day = 0.0; //minimum time of day in minutes since midnight
    double max_time_of_day = 0.0; //maximum time of day in minutes since midnight

    double min_pmdx = 0.0; //minimum p(-dx)
    double max_pmdx = 1.0; //maximum p(-dx)

    double min_ppdx = 0.0; //minimum p(+dx)
    double max_ppdx = 1.0; //maximum p(+dx)

    int min_size = 0; //minimum number of shares exchanged in the last filtered trade
    int max_size = 999999999; //minimum number of shares exchanged in the last filtered trade
};

struct feature
{
    std::string name;

    float mean = 0.0;
    float std = 0.0;
};

class MLModel //a class that contains the neural network used to make predictions and the ranges for each input feature
{
public:
    static const size_t input_length = 16;
    static const size_t output_length = 3;

    MLModel();
    ~MLModel();
    
    void loadWeights(const std::string&);
    void loadScales(const std::string&);

    //calculate the probability of a positive transition
    float predict(float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float);

    simpleNeuralNet<input_length, output_length> neural_net;
    inlierRanges ranges;

    float input[input_length];
    float means[input_length];
    float stds[input_length];
};

#endif
