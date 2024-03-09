
from sklearn.preprocessing import StandardScaler

import tensorflow
import pandas
import numpy
import time

import warnings

warnings.filterwarnings('ignore')

target = 'transition' # what we are trying to predict
base_dir = '' # directory that contains the transition data and outputs

trans_data_path = f'{base_dir}transitions.csv'

drawdown_paths = 100000 # number of simulations to run to get the maximum drawdown
potential_loss = 1000.0 # risk 1000 dollars per trade - this can be any positive number

# ordered the same way as the model inputs
features = ['time_of_day', 'relative_volume', 'n', 'mean', 'dp', 'std', 'dt', 'vsum', 'average_volume',
            'previous_days_close', 'rolling_csum', 'rolling_vsum', 'p(-dx)', 'size', 'p(+dx)', 'lambda']

num_features = len(features)

total_segments = 10 # total number of segments to perform walk-foward analysis on
train_segments = 8 # size of each train set in segments
test_segments = 1 # size of each test set in segments

segment_period = pandas.Timedelta('138D').value # span of a segment in nanoseconds

initial_capitals = numpy.array([5e+5, 1e+6, 5e+6, 1e+7, 5e+7, 1e+8, 5e+8, 1e+9]) # does not account for PDT

def transform(data, scaler, fit=False):
    
    for feature in ['vsum', 'rolling_vsum', 'rolling_csum', 'dt', 'dp', 'price', 'size', 'previous_days_close',
                    'average_volume', 'mean', 'std', 'p(-dx)', 'p(+dx)', 'lambda', 'relative_volume']:
        
        data[feature] = numpy.log(1e-9 + data[feature])
        
    if fit: data[features] = scaler.fit_transform(data[features])
    else:
        re_features = scaler.feature_names_in_
        data[re_features] = scaler.transform(data[re_features])
        
def inverse_transform(data, scaler):
    
    re_features = scaler.feature_names_in_
    
    data[re_features] = scaler.inverse_transform(data[re_features])
    
    for feature in ['vsum', 'rolling_vsum', 'rolling_csum', 'dt', 'dp', 'price', 'size', 'previous_days_close',
                    'average_volume', 'mean', 'std', 'p(-dx)', 'p(+dx)', 'lambda', 'relative_volume']:
        
        data[feature] = numpy.exp(data[feature]) - 1e-9
        
def remove_outliers(transformed_and_scaled_data):
    
    exclude = (transformed_and_scaled_data[features]**2 > 9).any(axis=1)
    
    transformed_and_scaled_data.drop(transformed_and_scaled_data[exclude].index, inplace=True)
    
    return exclude.sum()

def relu(x): return tensorflow.keras.backend.relu(x, alpha=0.1) # leaky relu
def neg_log_loss(true, pred): # negative log loss
    
    eps = tensorflow.keras.backend.epsilon()
    r_sum = tensorflow.keras.backend.sum(true * tensorflow.keras.backend.log(eps + pred), axis=-1)
    
    return -tensorflow.keras.backend.mean(r_sum)

# one-hot-encoding and inverse one-hot-encoding
def y_ohe(y): return numpy.array(y[...,None] == numpy.array([[-1,0,1]])[None,...], dtype=numpy.float32)[0,...]
def y_iohe(y): return (y * numpy.array([-1,0,1])[None,...]).sum(axis=-1)

def multi_pred(model, x): # non-binary class prediction - class is the one with the highest probability
    
    mp = numpy.array(model.predict(x, verbose=0))
    
    return numpy.array(mp == numpy.array(mp.max(axis=-1)[...,None]), dtype=numpy.float32)

def construct_and_train_nn(x, y, ff_dim0, ff_dim1, iters): # construct a neural net
    
    inputs = tensorflow.keras.layers.Input((len(x.columns),))
    
    for i in range(iters):
        if i == 0:
            dense0 = tensorflow.keras.layers.Dense(ff_dim0, activation=relu)(inputs)
            dense1 = tensorflow.keras.layers.Dense(ff_dim1, activation=relu)(dense0)
            
            continue
            
        dense0 = tensorflow.keras.layers.Dense(ff_dim0, activation=relu)(dense1)
        dense1 = tensorflow.keras.layers.Dense(ff_dim1, activation=relu)(dense0) + dense1
        
    outputs = tensorflow.keras.layers.Dense(3, activation='softmax')(dense1)
    model = tensorflow.keras.Model(inputs=inputs, outputs=outputs)
    adam = tensorflow.keras.optimizers.Adam(learning_rate=1e-3)
    
    model.compile(optimizer=adam, loss=neg_log_loss, metrics=['accuracy'])
    model.fit(x, y, epochs=100, batch_size=256, verbose=0)
    
    return model

# vector function to calculate quantum price levels
def quantum_price_levels(data, transition):
    
    n = data['n'].round(0) + transition
    
    nn = numpy.abs(n)
    l = data['lambda']
    
    c0 = -l * (1.1924 + 33.2383 * nn + 56.2169 * nn * nn) / (1 + 43.6196 * nn)
    c1 = (0.25 * c0 * c0 - 1 / 27) ** 0.5
    
    E = (2 * nn + 1) * ((-c0 / 2 + c1) ** (1 / 3) + (-c0 / 2 - c1) ** (1 / 3)) / data['E0']
    E = 1 + data['std'] * 0.21 * E
    
    return data['previous_days_close'] * E * (n >= 0) + data['previous_days_close'] / E * (n < 0)

def calc_profit_and_buy_signals(data, R): # calculate profit for potential trades and buy signals
    
    # if transition == 0, we don't know the last traded price so I assume that it's near the -1 transition price
    transition_dir = data['transition'].replace(0, -1, inplace=False)
    G = R * data['reward_per_share'] / data['risk_per_share']
    
    data['profit'] = (transition_dir == 1) * G - (transition_dir != 1) * R
    data['buy_signal'] = (data['chance_of_+1_transition'] * (G + R)) > R
    
def calc_risk_and_reward(data):
    
    data['lower_transition_price'] = quantum_price_levels(data, -1)
    data['upper_transition_price'] = quantum_price_levels(data, 1)
    data['current_price'] = quantum_price_levels(data, 0)

    data['slippage'] = data['last_ask'] - data['last_bid'] # estimate slippage using the spread of the most recent quote update
    data['risk_per_share'] = data['current_price'] - data['lower_transition_price'] + data['slippage']
    data['reward_per_share'] = data['upper_transition_price'] - data['current_price'] - data['slippage']
    
def find_max_drawdowns(profit, num_paths):
    
    max_drawdowns = []
    random_generator = numpy.random.default_rng()
    
    for i in range(num_paths):
        path = random_generator.permutation(profit, axis=-1).cumsum(axis=-1)
        cmax = numpy.maximum.accumulate(path)
        
        max_drawdowns.append((cmax - path).max())
        
    return numpy.array(max_drawdowns)

# determine the chance of exceeding a maximum loss
def cdf(losses, max_loss):
    
    return len(losses[losses >= max_loss]) / len(losses)

# turn transitions into buy and sell events so we can simulate the trading strategy
def get_events(data):
    
    events = data[data['buy_signal']]
    
    events.dropna(inplace=True)
    
    buy_events = events[['buy_price', 'n_time', 'risk_per_share']]
    sell_events = events[['sell_price', 't_time', 'risk_per_share']]
    
    buy_events['event'] = 1 # 1 for buy
    sell_events['event'] = 0 # 0 for sell
    
    buy_events['id'] = numpy.arange(len(events))
    sell_events['id'] = numpy.arange(len(events))
    
    buy_events.rename(columns={'buy_price':'price', 'n_time':'time'}, inplace=True)
    sell_events.rename(columns={'sell_price':'price', 't_time':'time'}, inplace=True)
    
    events = pandas.concat([buy_events, sell_events], axis=0)
    
    events.sort_values(['time', 'event'], ascending=[True, False], inplace=True)
    
    return events

# execute the trading strategy with some initial capital and return the total profit for different values
# does not account for cash settlement so initial_capital must be and stay over 25000 the whole time in ...
# ... order for this to be accurate, or we assume we always have 25000 in the account so we don't have to worry about that
def get_profits(events, initial_capitals):
    
    final_capitals = []
    
    for capital in initial_capitals:
        current_positions = {} # keep track of the stocks we are holding - each item = (event_id, position_size)
        
        for index, price, time_ns, risk, event_type, event_id in events.itertuples():
            if price < 1.0: price = round(price, 4)
            else: price = round(price, 2)
            
            if event_type: # if this is a buy event
                if capital >= price: # if there is at least enough cash for one share
                    position_size = (min(capital / price, potential_loss / risk)) // 1
                    
                    if position_size > 0:
                        capital -= position_size * price
                        
                        current_positions[event_id] = position_size
                        
            elif event_id in current_positions: # if we are already holding this position
                capital += current_positions[event_id] * price
                
                del current_positions[event_id]
                
        final_capitals.append(capital)
        
    return numpy.array(final_capitals)

def get_net_gains(data, initial_capitals):
    
    data['buy_price'] = data['current_price']
    data['sell_price'] = quantum_price_levels(data, data['transition'].replace(0, -1, inplace=False))
    
    return get_profits(get_events(data), initial_capitals)

def save_scaler_info(scaler, input_features, path):
    
    scale_features = list(scaler.feature_names_in_)
    scale_json = '['
    
    print('ORDER OF THE INPUTS\n')
    
    for feature in input_features:
        try:
            mean = scaler.mean_[scale_features.index(feature)]
            std = scaler.var_[scale_features.index(feature)] ** 0.5
            
            scale_json += '{' + f'"feature name": "{feature}", "mean": {mean}, "std": {std}' + '},'
            
            print(feature)
            
        except ValueError: pass # feature is not used in the data transformation
        
    scale_json = scale_json[:-1] + ']'
    
    file = open(path, 'w')
    file.write(scale_json)
    file.close()
    
def save_model_weights(model, path):
    
    jstring = '['
    
    for layer in model.layers:
        jstring += '{"weight_sets":['
        
        for weight in layer.weights:
            jstring += '{"shape":' + str(list(weight.shape))
            jstring += ', "weights":' + str(list(weight.numpy().ravel()))
            jstring += '}, '
            
        if len(layer.weights): jstring = jstring[:-2] + ']}, '
        else: jstring += ']}, '
        
    if len(model.layers): jstring = jstring[:-2] + ']'
    else: raise Exception("Cannot save an empty model.")
    
    file = open(path, 'w')
    file.write(jstring)
    file.close()
    
transition_data = pandas.read_csv(trans_data_path)
transition_data = transition_data[transition_data['n_time'] >= pandas.Timestamp('2020-01-01').value]

localized_time_of_day = transition_data['n_time'].astype('datetime64[ns]')
localized_time_of_day = localized_time_of_day.dt.tz_localize('UTC').dt.tz_convert('America/New_York')
localized_time_of_day = localized_time_of_day - localized_time_of_day.dt.floor('1D')
localized_time_of_day = localized_time_of_day.dt.total_seconds() / 60 # in minutes

transition_data = transition_data[localized_time_of_day < 955] # remove transitions that start after 3:55 pm EST

localized_time_of_day = transition_data['t_time'].astype('datetime64[ns]')
localized_time_of_day = localized_time_of_day.dt.tz_localize('UTC').dt.tz_convert('America/New_York')
localized_time_of_day = localized_time_of_day - localized_time_of_day.dt.floor('1D')
localized_time_of_day = localized_time_of_day.dt.total_seconds() / 60 # in minutes

transition_data.loc[localized_time_of_day >= 955, target] = 0 # set all transitions that end after 3:55 pm EST to 0

del localized_time_of_day

transition_data['time_of_day'] = transition_data['n_time'].astype('datetime64[ns]')
transition_data['time_of_day'] = transition_data['time_of_day'] - transition_data['time_of_day'].dt.floor('1D')
transition_data['time_of_day'] = transition_data['time_of_day'].dt.total_seconds() / 60 # in minutes

transition_data['relative_volume'] = transition_data['vsum'] / transition_data['average_volume']
transition_data['dp'] = transition_data['price'] / (transition_data['price'] - transition_data['dp'])

transition_data['transition'].clip(-1, 1, inplace=True)

transition_data = transition_data[transition_data['transition']**2 <= 1]
transition_data = transition_data[transition_data['rolling_csum'] >= 5]
transition_data = transition_data[transition_data['dt'] > 0.0]
transition_data = transition_data[transition_data['vsum'] > 0]
transition_data = transition_data[transition_data['average_volume'] > 0.0]

time_max = transition_data.n_time.max()
time_min = time_max - total_segments * segment_period

for segment in range((total_segments - train_segments) // test_segments):
    train_start = time_min + segment_period * segment * test_segments
    train_end = train_start + segment_period * train_segments
    
    test_start = train_end
    test_end = test_start + segment_period * test_segments
    
    train_data = transition_data[(transition_data.n_time > train_start) & (transition_data.n_time <= train_end)]
    test_data = transition_data[(transition_data.n_time > test_start) & (transition_data.n_time <= test_end)]
    
    scaler = StandardScaler()
    
    transform(train_data, scaler, True)
    transform(test_data, scaler)
    
    remove_outliers(train_data)
    remove_outliers(test_data)
    
    x_train = train_data[features]
    x_test = test_data[features]
    
    y_train = train_data[target]
    y_test = test_data[target]
    
    model = construct_and_train_nn(x_train, y_ohe(y_train), 32, 16, 3)
    
    inverse_transform(train_data, scaler)
    inverse_transform(test_data, scaler)

    train_data['chance_of_+1_transition'] = model.predict(x_train)[...,2]
    test_data['chance_of_+1_transition'] = model.predict(x_test)[...,2]
    
    del x_train
    del x_test
    
    del y_train
    del y_test
    
    calc_risk_and_reward(train_data)
    calc_risk_and_reward(test_data)

    train_data = train_data[(train_data['rolling_vsum'] * train_data['current_price'] >= 10000) & (train_data['rolling_csum'] >= 10)]
    test_data = test_data[(test_data['rolling_vsum'] * test_data['current_price'] >= 10000) & (test_data['rolling_csum'] >= 10)]
    
    calc_profit_and_buy_signals(train_data, potential_loss)
    calc_profit_and_buy_signals(test_data, potential_loss)
    
    train_data.dropna(inplace=True)
    test_data.dropna(inplace=True)
    
    train_losses = find_max_drawdowns(train_data[train_data['buy_signal']]['profit'], drawdown_paths)
    test_losses = find_max_drawdowns(test_data[test_data['buy_signal']]['profit'], drawdown_paths)
    
    test_final_capitals = get_net_gains(test_data, initial_capitals)
    max_loss = train_losses.max()
    
    print('\n\nFROM ', pandas.Timestamp(train_start), ' TO ', pandas.Timestamp(test_end))
    print('MAXIMIM LOSS ON TRAIN SET :', max_loss)
    print(f'CHANCE OF EXCEEDING {max_loss} IN LOSSES FOR TEST SET :', cdf(test_losses, max_loss), '\n')
    print('NET GAINS FOR THIS TEST SEGMENT')
    
    for i in range(len(initial_capitals)): print('INITIAL :', initial_capitals[i], '---', 'FINAL :', test_final_capitals[i])
    
train_start = time_max - segment_period * train_segments
train_data = transition_data[transition_data.n_time > train_start]

scaler = StandardScaler()
    
transform(train_data, scaler, True)

remove_outliers(train_data)

x_train = train_data[features]
y_train = train_data[target]

model = construct_and_train_nn(x_train, y_ohe(y_train), 32, 16, 3)

inverse_transform(train_data, scaler)

train_data['chance_of_+1_transition'] = model.predict(x_train)[...,2]

del x_train
del y_train

calc_risk_and_reward(train_data)
calc_profit_and_buy_signals(train_data, potential_loss)

train_data.dropna(inplace=True)

train_losses = find_max_drawdowns(train_data[train_data['buy_signal']]['profit'], drawdown_paths)

last_max_loss = max_loss
max_loss = train_losses.max()

print('\n\nFROM ', pandas.Timestamp(train_start), ' TO ', pandas.Timestamp(test_end))
print('MAXIMIM LOSS ON TRAIN SET :', round(max_loss, 2))
print(f'EXPECTED CHANCE OF EXCEEDING {round(max_loss, 2)} :', cdf(test_losses, last_max_loss), '\n')

save_scaler_info(scaler, features, f'{base_dir}scaler_info.json')
save_model_weights(model, f'{base_dir}model_weights.json')
