
import multiprocessing
import threading
import requests
import pandas
import numpy
import json
import time
import math
import os

import warnings

warnings.filterwarnings('ignore')

bins = 51 # number of bins to use for return distributions for qpl calculations
std_max = 3 # cutoff relative change to use for change distributions
lookback = 1024 # in days

apiKey = 'ENTER-API-KEY'
secretKey = 'ENTER-SECRET-KEY'

base_url = 'https://data.alpaca.markets/v2/stocks/'
stock_url = 'https://paper-api.alpaca.markets/v2/assets'

headers = {'APCA-API-KEY-ID':apiKey, 'APCA-API-SECRET-KEY':secretKey}

one_day = pandas.Timedelta('1Day')
one_hour = pandas.Timedelta('1Hour')
one_minute = pandas.Timedelta('1Min')
one_second = pandas.Timedelta('1S')

end = pandas.Timestamp.today('UTC').floor('1D')
start = end - (5 * 366 + int(1.5 * lookback)) * one_day

start_date_trans = start + int(1.5 * lookback) * one_day

start_date = start.strftime('%Y-%m-%d')
end_date = end.strftime('%Y-%m-%d')

out_file = 'transitions.csv'

window_sizes = ['0.5', '2', '10', '60'] # window sizes in seconds for calculating various features
lag_times = [1, 5, 20, 60] # lag times in minutes for calculating relative price changes on longer timeframes

class RetryException(Exception): pass # raise when we want to retry a request
class RequestException(Exception): pass # raise when we get a status code we didn't expect or account for

def window_diff(rows : list) -> float or int: return rows[-1] - rows[0]

# symbol names being strings take up a lot of memory so we convert them to integers with encode_symbol
def encode_symbol(symbol : str) -> int: return sum([27**i * (ord(j)-64) for i,j in enumerate(symbol)])
def decode_symbol(encoding : int) -> str:
    
    if encoding == 1: return 'A'
    
    max_encoding = math.ceil(math.log(encoding,27))
    symbol = ''
    
    for i in range(max_encoding):
        symbol = chr(64 + encoding // 27**(max_encoding-i-1)) + symbol
        encoding %= 27**(max_encoding-i-1)
        
    return symbol

def get_price_level_func(l : float, E0 : float, std : float, previous_days_close : float) -> callable:
    
    def E(n : int) -> float:
        
        nn = n if n >= 0 else -n
        
        c0 = -l * (1.1924 + 33.2383 * nn + 56.2169 * nn * nn) / (1 + 43.6196 * nn)
        c1 = (0.25 * c0 ** 2 - 1 / 27) ** 0.5
        
        E = (2 * nn + 1) * ((-c0 / 2 + c1) ** (1 / 3) + (-c0 / 2 - c1) ** (1 / 3)) / E0
        EE = 1 + 0.21 * std * E
        
        return previous_days_close * EE if n >= 0 else previous_days_close / EE
    
    return E

def execute_thread_pool(n_threads : int, function : callable, *args, **kwargs) -> None:
    
    threads = [threading.Thread(target=function, args=args, kwargs=kwargs) for i in range(n_threads-1)]
    
    for thread in threads: thread.start()
    
    function(*args, **kwargs)
    
    for thread in threads: thread.join()
    
def execute_process_pool(n_processes : int, function : callable, *args, **kwargs) -> None:
    
    processes = [multiprocessing.Process(target=function, args=args, kwargs=kwargs) for i in range(n_processes-1)]
    
    for process in processes: process.start()
    
    function(*args, **kwargs)
    
    for process in processes: process.join()
    
def get_data_for(params : dict, symbol : str, data_type : str) -> pandas.DataFrame or None:
    
    page_token = 1 # any non-zero value to allow the loop to start
    
    data = []
    
    while page_token:
        request = requests.get(base_url + data_type, params=params, headers=headers)
        
        if request.status_code != 200:
            
            # [429, 500, 502, 504] : exceeded rate limit, internal server error, bad gateway, gateway timeout
            if request.status_code in [429, 500, 502, 504]:
                time.sleep(5.0)
                
                raise RetryException() # resubmit the request
                
            raise RequestException(f'Request returned status code {request.status_code}: {request.text}')
            
        new_data = json.loads(request.text)
        
        page_token = new_data['next_page_token']
        
        if symbol in new_data[data_type]: data.extend(new_data[data_type][symbol])
        
        params['page_token'] = page_token
        
    data = pandas.DataFrame(data=data)
    
    data['t'] = data['t'].astype('datetime64[ns]').dt.tz_localize('UTC')
    
    data.set_index('t', inplace=True)
    
    return data

def get_bars_for(symbol : str, timeframe : str, start : str, end : str, adjustment : str) -> pandas.DataFrame or None:
    
    params = {'timeframe':timeframe, 'start':start, 'end':end, 'limit':10000,
              'adjustment':adjustment, 'feed':'sip', 'symbols':symbol, 'page_token':None}
    
    return get_data_for(params, symbol, 'bars')

def get_trades_for(symbol : str, start : str, end : str) -> pandas.DataFrame or None:
    
    params = {'start':start, 'end':end, 'limit':10000, 'feed':'sip', 'symbols':symbol, 'page_token':None}
    
    return get_data_for(params, symbol, 'trades')

def get_quotes_for(symbol : str, start : str, end : str) -> pandas.DataFrame or None:
    
    params = {'start':start, 'end':end, 'limit':10000, 'feed':'sip', 'symbols':symbol, 'page_token':None}
    
    return get_data_for(params, symbol, 'quotes')

def get_daily_features(symbol : str) -> pandas.DataFrame or None:
    
    """
    For the quantum price levels I required there to be at least 500 days for any calculation.
    """
    
    daily_data = get_bars_for(symbol, '1Day', start_date, end_date, 'all')
    raw_daily_data = get_bars_for(symbol, '1Day', start_date, end_date, 'raw')
    
    daily_data['cash_traded'] = daily_data['vw'] * daily_data['v']
    
    del daily_data['o'] # faster than daily_data.drop
    del daily_data['h']
    del daily_data['l']
    del daily_data['vw']
    
    del raw_daily_data['o']
    del raw_daily_data['h']
    del raw_daily_data['l']
    del raw_daily_data['vw']
    del raw_daily_data['n']
    
    daily_data['average_volume'] = daily_data['v'].shift(1).rolling(70, min_periods=1).mean()
    daily_data['average_trade_count'] = daily_data['n'].shift(1).rolling(70, min_periods=1).mean()
    daily_data['average_cash_traded'] = daily_data['cash_traded'].shift(1).rolling(70, min_periods=1).mean()
    
    daily_data['price_ratio'] = daily_data['c'] / raw_daily_data['c']
    daily_data['volume_ratio'] = daily_data['v'] / raw_daily_data['v']
    
    del daily_data['cash_traded']
    del daily_data['n']
    
    del raw_daily_data
    
    previous_days_close = daily_data['c'].shift(1)
    relative_change = numpy.array(daily_data['c'] / previous_days_close)
    
    min_values = 500
    
    has_min_values = numpy.arange(len(relative_change)) >= min_values
    
    indices = numpy.arange(len(relative_change))[:,None] + numpy.arange(lookback)[None,:] - lookback + 1
    indices = indices.clip(min=0)
    
    values = numpy.insert(relative_change[:-1], 0, numpy.nan)
    values = numpy.take(values, indices)
    
    del indices
    
    wheres = numpy.isfinite(values)
    
    means = values.mean(axis=1, where=wheres)[:,None]
    stds = values.std(axis=1, where=wheres)[:,None]
    
    del wheres
    
    inliers = (values - means)**2 <= (std_max**2 * stds**2)
    
    r_mins = values.min(axis=1, where=inliers, initial=numpy.inf)[:,None]
    r_maxs = values.max(axis=1, where=inliers, initial=-numpy.inf)[:,None]
    
    valid_ranges = (r_maxs > r_mins)[:,0]
    
    r_scales = (bins - 1) / (r_maxs - r_mins)
    
    count_inliers = inliers.sum(axis=1)
    bin_indices = (r_scales * (values - r_mins)) // 1
    
    del inliers
    
    dr = 2 * std_max * stds / bins
    
    drp = (r_scales * (means + dr - r_mins)) // 1
    drm = (r_scales * (means - dr - r_mins)) // 1
    
    del r_mins
    del r_maxs
    
    del r_scales
    
    pp = (bin_indices==drp).sum(axis=1) / count_inliers
    pm = (bin_indices==drm).sum(axis=1) / count_inliers
    
    del drp
    del drm
    
    del count_inliers
    del bin_indices
    
    means = means[:,0]
    stds = stds[:,0]
    dr = dr[:,0]
    
    l = numpy.abs(((means - dr)**2 * pm - (means + dr)**2 * pp) / ((means + dr)**4 * pp - (means - dr)**4 * pm)) # lambda
    
    # l >= 0.35 to prevent complex numbers
    
    keep = has_min_values & valid_ranges & (stds > 0) & (previous_days_close > 0) & (l >= 0.35)
    
    c0_E0 = -1.1924 * l
    c1_E0 = (c0_E0**2 / 4 - 1 / 27)**0.5
    
    E0 = (-c0_E0/2 + c1_E0)**(1/3) + (-c0_E0/2 - c1_E0)**(1/3) # E(n=0)
    
    daily_data['previous_days_close'] = previous_days_close
    daily_data['mean'] = means
    daily_data['std'] = stds
    daily_data['p(-dx)'] = pm
    daily_data['p(+dx)'] = pp
    daily_data['E0'] = E0
    daily_data['lambda'] = l
    
    del daily_data['v']
    del daily_data['c']
    
    return daily_data[keep & numpy.isfinite(daily_data).all(axis=1)]

def get_transitions_for(symbol : str, daily_data : pandas.DataFrame, lock : multiprocessing.Lock()) -> None:
    
    encoded_symbol = encode_symbol(symbol)
    
    for (date, average_volume, average_trade_count, average_cash_traded, price_ratio, volume_ratio,
         previous_days_close, mean, std, pm, pp, E0, l) in daily_data.itertuples():
        
        while True:
            try:
                minute_data = get_bars_for(symbol, '1Min', date.strftime('%Y-%m-%dT00:00:00Z'),
                                           (date + one_day).strftime('%Y-%m-%dT00:00:00Z'), 'all')
                
                minute_data['cash'] = minute_data['vw'] * minute_data['v']
                minute_data['vsum'] = minute_data['v'].cumsum()
                minute_data['csum'] = minute_data['n'].cumsum()
                minute_data['cash_sum'] = minute_data['cash'].cumsum()
                
                minute_data['high_of_day'] = minute_data['h'].cummax()
                minute_data['low_of_day'] = -minute_data['l'].cummin() # needs to be negitive to record low_of_day_time
                
                minute_data['time'] = minute_data.index
                minute_data['time'] = minute_data['time'].astype(numpy.uint64)
                
                # record the time - to the nearest minute - that the high of day occured
                high_of_day_time = minute_data.groupby('high_of_day', as_index=False)[['high_of_day','time']].first()
                high_of_day_time.rename(columns={'time':'high_of_day_time'}, inplace=True)
                
                # record the time - to the nearest minute - that the low of day occured
                low_of_day_time = minute_data.groupby('low_of_day', as_index=False)[['low_of_day','time']].first()
                low_of_day_time.rename(columns={'time':'low_of_day_time'}, inplace=True)
                
                high_of_day_time = pandas.merge_asof(minute_data, high_of_day_time, on='high_of_day',
                                                     allow_exact_matches=True, direction='backward')['high_of_day_time']
                
                low_of_day_time = pandas.merge_asof(minute_data, low_of_day_time, on='low_of_day',
                                                    allow_exact_matches=True, direction='backward')['low_of_day_time']
                
                minute_data['low_of_day'] = -minute_data['low_of_day'] # changing low_of_day price back to positive
                minute_data['high_of_day_time'] = numpy.uint64(high_of_day_time)
                minute_data['low_of_day_time'] = numpy.uint64(low_of_day_time)
                
                for lag_time in lag_times:
                    minute_data['c_'+str(lag_time)+'m'] = minute_data['c']
                    minute_data['t_'+str(lag_time)+'m'] = minute_data.index + one_minute
                    minute_data['t_'+str(lag_time)+'m'] = minute_data['t_'+str(lag_time)+'m'].astype(numpy.uint64)
                    
                del minute_data['time']
                del minute_data['cash']
                del minute_data['o'] # faster than minute_data.drop
                del minute_data['h']
                del minute_data['l']
                del minute_data['vw']
                del minute_data['n']
                del minute_data['v']
                del minute_data['c']
                
                tick_data = get_trades_for(symbol, date.strftime('%Y-%m-%dT00:00:00Z'),
                                           (date + one_day).strftime('%Y-%m-%dT00:00:00Z'))
                
                tick_data_size = tick_data.memory_usage(index=True, deep=True).sum() # data size in bytes
                
                if not len(tick_data): break
                
                tick_data['p'] *= price_ratio # adjust tick data for splits and dividends
                tick_data['s'] *= volume_ratio # adjust volume for splits (should be close to 1.0 / price_ratio)
                
                tick_data.sort_index(inplace=True)
                
                tick_data['m'] = tick_data.index - one_minute
                
                tick_data = pandas.merge_asof(tick_data, minute_data, left_on='m', right_index=True,
                                              allow_exact_matches=False, direction='backward')
                for lag_time in lag_times:
                    del tick_data['c_'+str(lag_time)+'m']
                    del tick_data['t_'+str(lag_time)+'m']
                    
                    tick_data['m'] = tick_data.index - lag_time * one_minute
                    
                    tick_data = pandas.merge_asof(tick_data, minute_data[['c_'+str(lag_time)+'m', 't_'+str(lag_time)+'m']],
                                                  left_on='m', right_index=True, allow_exact_matches=False,
                                                  direction='backward')
                    
                tick_data = tick_data[tick_data['s'] >= 100]
                tick_data = tick_data[tick_data['x'] != 'D'] # FINRA
                tick_data = tick_data[~tick_data['c'].astype(str).str.contains("'Z'")] # out of seq (reg)
                tick_data = tick_data[~tick_data['c'].astype(str).str.contains("'U'")] # out of seq (pre/post)
                
                if not len(tick_data): break
                
                del tick_data['x'] # faster than tick_data.drop
                del tick_data['c']
                del tick_data['i']
                del tick_data['z']
                
                if 'u' in tick_data.columns: del tick_data['u']
                
                tick_data.dropna(inplace=True)
                
                if not len(tick_data): break
                
                del tick_data['m']
                del minute_data
                
                # for time periods in rolling windows, rolling period is less than (not less than or equal to) ...
                # ... specified periods (0.5, 2, 10, and 60 seconds in this case)
                
                tick_data['ns'] = tick_data.index
                tick_data['ns'] = tick_data['ns'].astype(numpy.uint64)
                
                for window_size in window_sizes:
                    window = tick_data.rolling(window_size + 'S', min_periods=1)
                    
                    tick_data['rolling_vsum_' + window_size + 'S'] = window['s'].sum()
                    tick_data['rolling_csum_' + window_size + 'S'] = window['s'].count()
                    
                    tick_data['dp_' + window_size + 'S'] = window['p'].apply(window_diff, raw=True)
                    tick_data['dt_' + window_size + 'S'] = 1e-9 * window['ns'].apply(window_diff, raw=True)
                    
                tick_data.set_index('ns', inplace=True)
                
                # gather transitions
                
                transitions = []
                
                get_price_level = get_price_level_func(l, E0, std, previous_days_close)
                
                if get_price_level is None: break
                
                tick_data.rename(columns={'p':'price', 's':'size'}, inplace=True)
                
                price = tick_data['price'].iloc[0]
                n = 0
                
                while price <= get_price_level(n-1): n -= 1
                while price >= get_price_level(n+1): n += 1
                
                n_tick = dict(tick_data.iloc[0])
                n_tick['n_time'] = tick_data.index[0]
                
                other_feature_names = list(tick_data.columns)[1:] # the first one is price
                
                for tick_time, price, *other_features in tick_data.itertuples():
                    
                    new_n = n
                    
                    while price <= get_price_level(new_n-1): new_n -= 1
                    while price >= get_price_level(new_n+1): new_n += 1
                    
                    if new_n != n:
                        n_tick.update({'n':n, 'transition':new_n-n, 't_time':tick_time})
                        
                        transitions.append(n_tick)
                        
                        n = new_n
                        n_tick = {'n_time':tick_time, 'price':price}
                        
                        n_tick.update(zip(other_feature_names, other_features))
                        
                n_tick.update({'n':n, 'transition':new_n-n, 't_time':tick_time})
                
                transitions.append(n_tick)
                
                transitions = pandas.DataFrame(data=transitions)
                
                transitions['n_time'] = transitions['n_time'].astype(numpy.uint64)
                
                last_transition_time = pandas.Timestamp(transitions['n_time'].iloc[-1])
                
                quote_data = get_quotes_for(symbol, date.strftime('%Y-%m-%dT00:00:00Z'),
                                            last_transition_time.strftime('%Y-%m-%dT%H:%M:%S.%fZ'))
                
                if not len(quote_data): break
                
                quote_data_size = quote_data.memory_usage(index=True, deep=True).sum()
                
                quote_data['bp'] *= price_ratio # adjust quote bid data for splits and dividends
                quote_data['ap'] *= price_ratio # adjust quote ask data for splits and dividends
                
                quote_data['bs'] *= volume_ratio # adjust bid size for splits (should be close to 1.0 / price_ratio)
                quote_data['as'] *= volume_ratio # adjust ask size for splits (should be close to 1.0 / price_ratio)
                
                del quote_data['ax']
                del quote_data['bx']
                
                del quote_data['c']
                del quote_data['z']
                
                quote_data.sort_index(inplace=True)
                
                quote_data['ns_q'] = quote_data.index
                quote_data['ns_q'] = quote_data['ns_q'].astype(numpy.uint64)
                
                transitions = pandas.merge_asof(transitions, quote_data, left_on='n_time', right_on='ns_q',
                                                allow_exact_matches=False, direction='backward')
                del quote_data
                
                transitions.rename(columns={'bp':'last_bid_price', 'ap':'last_ask_price',
                                            'bs':'last_bid_size', 'as':'last_ask_size', 'ns_q':'q_time'}, inplace=True)
                
                transitions['throughtput'] = (tick_data_size + quote_data_size) / len(transitions)
                transitions['previous_days_close'] = previous_days_close
                transitions['average_trade_count'] = average_trade_count
                transitions['average_cash_traded'] = average_cash_traded
                transitions['average_volume'] = average_volume
                transitions['symbol'] = encoded_symbol
                transitions['mean'] = mean
                transitions['std'] = std
                transitions['p(-dx)'] = pm
                transitions['p(+dx)'] = pp
                transitions['lambda'] = l
                transitions['E0'] = E0
                
                lock.acquire()
                
                while True:
                    try:
                        if os.path.exists(out_file): transitions.to_csv(out_file, mode='a', index=False, header=False)
                        else: transitions.to_csv(out_file, mode='w', index=False, header=True)
                        
                    except PermissionError:
                        time.sleep(5.0)
                        
                        continue
                        
                    break
                    
                lock.release()
                
            except (requests.ConnectionError, RetryException): continue
            except KeyError: pass # print('NO TICK DATA FOUND FOR', symbol, 'ON', date)
            
            break
            
def transitions_process(symbol_queue : multiprocessing.Queue, lock : multiprocessing.Lock()) -> None:
    
    while not symbol_queue.empty():
        try: symbol = symbol_queue.get(timeout=1.0)
        except: return
        
        try:
            daily_features = get_daily_features(symbol)
            daily_features = daily_features[daily_features.index > start_date_trans]
            
            get_transitions_for(symbol, daily_features, lock)
            
        except (requests.ConnectionError, RetryException): symbol_queue.put(symbol)
        except KeyError: print('NO DAILY DATA FOUND FOR', symbol)
        
        print(symbol_queue.qsize(), 'SYMBOLS LEFT TO EVALUATE')
        
def get_all_symbols(exchanges : list[str]) -> list[str]:
    
    params = {'status':None, 'asset_class':'us_equity'}
    
    symbols = json.loads(requests.get(stock_url, params=params, headers=headers).text)
    symbols = [i['symbol'] for i in symbols if i['exchange'] in exchanges]
    symbols = [i for i in symbols if i.isalpha()]
    
    delisted = [i[:-8] for i in symbols if i.isalpha() and i.endswith('DELISTED')]
    
    symbols = [i for i in symbols if not i.endswith('DELISTED')] + delisted
    
    return [symbol for symbol in symbols if not any([j.islower() for j in symbol])]

if __name__ == '__main__':
    t = time.time()
    
    n_threads = 5
    n_processes = 6
    exchanges = ['NASDAQ', 'NYSE']
    
    symbol_queue = multiprocessing.Queue()
    lock = multiprocessing.Lock()
    
    if os.path.exists(out_file): os.remove(out_file)
    for symbol in get_all_symbols(exchanges): symbol_queue.put(symbol)
    
    execute_process_pool(n_processes, execute_thread_pool, n_threads, transitions_process, symbol_queue, lock)
    
    print('FINISHED IN', round((time.time() - t) / 3600, 4), 'HOURS.')
    
