const Soup = imports.gi.Soup;
const _httpSession = new Soup.SessionAsync();
Soup.Session.prototype.add_feature.call(_httpSession, new Soup.ProxyResolverDefault());

function GeoNames(station) {
    this.station = station;
}

GeoNames.prototype = {
    getWeather: function(callback) {
        var request = Soup.Message.new('GET', 'http://api.geonames.org/weatherIcaoJSON?ICAO=' + this.station + '&username=demo');
        _httpSession.queue_message(request, function(_httpSession, message) {
            if (message.status_code !== 200) {
                callback(message.status_code, null);
                return;
            }
            var weatherJSON = request.response_body.data;
            var weather = JSON.parse(weatherJSON);
            callback(null, weather);
        });
    },

    getIcon: function(weather) {
        switch (weather.weatherObservation.weatherCondition) {
        case 'drizzle':
        case 'light showers rain':
        case 'light rain':
            return 'weather-showers-scattered.svg';
        case 'rain':
            return 'weather-showers.svg';
        case 'light snow':
        case 'snow grains':
            return 'weather-snow.svg';
        }
        switch (weather.weatherObservation.clouds) {
        case 'few clouds':
        case 'scattered clouds':
            return 'weather-few-clouds.svg';
        case 'clear sky':
            return 'weather-clear.svg';
        case 'broken clouds':
        case 'overcast':
            return 'weather-overcast.svg';
        }
        return 'weather-fog.svg';
    }
};
