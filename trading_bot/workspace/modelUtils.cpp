
#include "modelUtils.h"

void appendWeights(const std::string& weight_string, layerWeightContainer& layer_weights)
{
    weightArray weight_array;
    std::string current_float;

    for (const char& c : weight_string)
    {
        if (c == ',')
        {
            weight_array.push_back(std::stof(current_float));
            current_float.clear();
        }
        else if (c != ' ') current_float += c;
    }

    layer_weights.push_back(weight_array);
}

void parseLayerWeights(std::string& weight_string, const std::string& key, const std::string& value)
{
    if (key == "weights") weight_string = value.substr(1, weight_string.size() - 2) + ',';
}

void layerUpdateFunc(const std::string& weight_sets, weightContainer& weights)
{
    JSONArrayParser<std::string, layerWeightContainer, parseLayerWeights, appendWeights> layerParser;
    layerWeightContainer layer_weights;

    std::string weight_sets__ = weight_sets;

    try
    {
        preProcessJSONArray(weight_sets__);

        layerParser.parseJSONArray(weight_sets__, layer_weights);

        weights.push_back(layer_weights);
    }
    catch (std::exception& exception) //input and addition layers has no weights
    {
        //std::cout << exception.what() << std::endl;
    }
}

void loadLayerWeights(std::string& weight_sets, const std::string& key, const std::string& value)
{
    if (key != "weight_sets") throw exceptions::exception("Received an unexpected input field when loading model weights.");

    weight_sets = value;
}

void assignScale(const feature& current_feature, auto& min_bound, auto& max_bound, const size_t index, MLModel& model)
{
    min_bound = current_feature.mean - 3.0F * current_feature.std;
    max_bound = current_feature.mean + 3.0F * current_feature.std;

    model.means[index] = current_feature.mean;
    model.stds[index] = current_feature.std;
}

void assignLogScale(const feature& current_feature, auto& min_bound, auto& max_bound, const size_t index, MLModel& model)
{
    //lognormally distributed variables were transformed using ln(variable + 1e-9) and then the mean and stds were calculated

    min_bound = std::exp(current_feature.mean - 3.0F * current_feature.std) - 1e-9;
    max_bound = std::exp(current_feature.mean + 3.0F * current_feature.std) - 1e-9;

    model.means[index] = current_feature.mean;
    model.stds[index] = current_feature.std;
}

void setInputScale(const feature& current_feature, MLModel& model)
{
    if (current_feature.name == "time_of_day") assignScale(current_feature, model.ranges.min_time_of_day, model.ranges.max_time_of_day, 0, model);
    else if (current_feature.name == "relative_volume")
    {
        assignLogScale(current_feature, model.ranges.min_rvol, model.ranges.max_rvol, 1, model);

        if (model.ranges.min_rvol < 0.0F) model.ranges.min_rvol = 0.0F;
    }
    else if (current_feature.name == "n") assignScale(current_feature, model.ranges.min_n, model.ranges.max_n, 2, model);
    else if (current_feature.name == "mean") assignLogScale(current_feature, model.ranges.min_mean, model.ranges.max_mean, 3, model);
    else if (current_feature.name == "dp") assignLogScale(current_feature, model.ranges.min_dp, model.ranges.max_dp, 4, model);
    else if (current_feature.name == "std") assignLogScale(current_feature, model.ranges.min_std, model.ranges.max_std, 5, model);
    else if (current_feature.name == "dt") assignLogScale(current_feature, model.ranges.min_dt, model.ranges.max_dt, 6, model);
    else if (current_feature.name == "vsum") assignLogScale(current_feature, model.ranges.min_vsum, model.ranges.max_vsum, 7, model);
    else if (current_feature.name == "average_volume") assignLogScale(current_feature, model.ranges.min_average_volume, model.ranges.max_average_volume, 8, model);
    else if (current_feature.name == "previous_days_close")
    {
        assignLogScale(current_feature, model.ranges.min_previous_days_closing_price, model.ranges.max_previous_days_closing_price, 9, model);
    }
    else if (current_feature.name == "rolling_csum")
    {
        assignLogScale(current_feature, model.ranges.rolling_period_min_trades, model.ranges.rolling_period_max_trades, 10, model);

        if (model.ranges.rolling_period_min_trades < 5) model.ranges.rolling_period_min_trades = 5;
    }
    else if (current_feature.name == "rolling_vsum") assignLogScale(current_feature, model.ranges.rolling_volume_min, model.ranges.rolling_volume_max, 11, model);
    else if (current_feature.name == "p(-dx)") assignLogScale(current_feature, model.ranges.min_pmdx, model.ranges.max_pmdx, 12, model);
    else if (current_feature.name == "size") assignLogScale(current_feature, model.ranges.min_size, model.ranges.max_size, 13, model);
    else if (current_feature.name == "p(+dx)") assignLogScale(current_feature, model.ranges.min_ppdx, model.ranges.max_ppdx, 14, model);
    else if (current_feature.name == "lambda")
    {
        assignLogScale(current_feature, model.ranges.min_lambda, model.ranges.max_lambda, 15, model);

        if (model.ranges.min_lambda < 0.35F) model.ranges.max_lambda = 0.35F;
    }
    else throw exceptions::exception("Received an unknown input parameter.");
}

void loadInputScale(feature& current_feature, const std::string& key, const std::string& value)
{
    if (key == "feature name") current_feature.name = value;
    else if (key == "mean") current_feature.mean = std::stof(value);
    else if (key == "std") current_feature.std = std::stof(value);
}

MLModel::MLModel() {}
MLModel::~MLModel() {}

void MLModel::loadWeights(const std::string& file_path)
{
    std::ifstream file(file_path); //input file stream

    if (!file.is_open()) throw exceptions::exception("Failed open the file : " + file_path);

    std::string line;
    std::string content; //write the json here

    while (std::getline(file, line)) content += line;

    file.close();

    JSONArrayParser<std::string, weightContainer, loadLayerWeights, layerUpdateFunc> layerParser;
    weightContainer model_weights;

    preProcessJSONArray(content);

    layerParser.parseJSONArray(content, model_weights);

    if (model_weights.size() != 7) throw exceptions::exception("Received an unexpected number of layers.");

    neural_net.layer0.setWeights(model_weights[0][0], model_weights[0][1]);
    neural_net.layer1.setWeights(model_weights[1][0], model_weights[1][1]);
    neural_net.layer2.setWeights(model_weights[2][0], model_weights[2][1]);
    neural_net.layer3.setWeights(model_weights[3][0], model_weights[3][1]);
    neural_net.layer4.setWeights(model_weights[4][0], model_weights[4][1]);
    neural_net.layer5.setWeights(model_weights[5][0], model_weights[5][1]);
    neural_net.layer6.setWeights(model_weights[6][0], model_weights[6][1]);
}

void MLModel::loadScales(const std::string& file_path)
{
    std::ifstream file(file_path); //input file stream

    if (!file.is_open()) throw exceptions::exception("Failed open the file : " + file_path);

    std::string line;
    std::string content; //write the json here

    while (std::getline(file, line)) content += line;

    file.close();

    JSONArrayParser<feature, MLModel, loadInputScale, setInputScale> layerParser;
    
    preProcessJSONArray(content);

    layerParser.parseJSONArray(content, *this);
}

float MLModel::predict(float time_of_day, float relative_volume, float n, float mean, float dp, float std, float dt, float vsum, float average_volume,
    float previous_days_close, float rolling_csum, float rolling_vsum, float pmdx, float size, float ppdx, float lambda)
{
    input[0] = (time_of_day - means[0]) / stds[0]; //time of day
    input[1] = (std::log(1e-9 + relative_volume) - means[1]) / stds[1]; //relative volume
    input[2] = (n - means[2]) / stds[2]; //n
    input[3] = (std::log(1e-9 + mean) - means[3]) / stds[3]; //mean
    input[4] = (std::log(1e-9 + dp) - means[4]) / stds[4]; //dp
    input[5] = (std::log(1e-9 + std) - means[5]) / stds[5]; //std
    input[6] = (std::log(1e-9 + dt) - means[6]) / stds[6]; //dt
    input[7] = (std::log(1e-9 + vsum) - means[7]) / stds[7]; //vsum
    input[8] = (std::log(1e-9 + average_volume) - means[8]) / stds[8]; //average_volume
    input[9] = (std::log(1e-9 + previous_days_close) - means[9]) / stds[9]; //previous_days_close
    input[10] = (std::log(1e-9 + rolling_csum) - means[10]) / stds[10]; //rolling_csum
    input[11] = (std::log(1e-9 + rolling_vsum) - means[11]) / stds[11]; //rolling_vsum
    input[12] = (std::log(1e-9 + pmdx) - means[12]) / stds[12]; //p(-dx)
    input[13] = (std::log(1e-9 + size) - means[13]) / stds[13]; //size
    input[14] = (std::log(1e-9 + ppdx) - means[14]) / stds[14]; //p(+dx)
    input[15] = (std::log(1e-9 + lambda) - means[15]) / stds[15]; //lambda
    
    neural_net(input);

    return neural_net.output[2]; //first, second, and third elements are the probabilities of -1, 0, and +1 transitions respectively
}
