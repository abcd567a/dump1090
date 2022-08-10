/**
 * Socket-ized fetcher class
 */
var SkyAwareDataFetcher = function(opts){
	this.init(opts);
};

// Constructor
SkyAwareDataFetcher.prototype.init = function(opts){
	this._opts = opts;

	/**
	 * Upstream, we set a receiverData global var which contains stuff we need
	 * to get our socket connection going. It's not the best way to handle this
	 * data, but it works for this first implementation.
	 */

	// If receiverData isn't set then we can't do anything
	if (typeof EntryPoint.receiverData == 'undefined') {
		alert('Error fetching ADS-B sites for display');
		return;
	}

	// Local storage ref for receiverData
	this._opts.receiverData = EntryPoint.receiverData;

	// Ensure the current user has sites to view; if not, send them elsewhere
	if (this._opts.receiverData.locations.length == 0) {
		alert('No matching sites found to display')
		return;
	}

	/**
	 * Set up an interval to send batched updates to SkyAware; default is 1 sec
	 * but this can be overriden in the receiver config.
	 */
	var interval = 1000;
	if (typeof this._opts.receiverData.interval == 'number') {
		interval = this._opts.receiverData.interval;
	}

	setInterval($.proxy(this.dispatchData, this), interval);

	// Finally, set up the socket client
	this.setupSocket();
};

/**
 * This defines how we'll try reconnects if the socket closes unexpectedly. We
 * want to try the first reconnect fairly quickly, and then back off if we keep
 * having issues. If we try enough times to no avail, we'll surrender.
 */
SkyAwareDataFetcher.prototype._reconnectConfig = [
	1000,
	5000,
	15000,
	60000,
	120000
];

// Reconnect attempt counter
SkyAwareDataFetcher.prototype._reconnectAttempts = 0;

// Wrapper for setting up a new socket connection
SkyAwareDataFetcher.prototype.setupSocket = function(){
	// We need a reference to the fetcher object we can use within socket callbacks
	var fetcher = this;

	// Set up socket
	var socket = new WebSocket('wss://' + fetcher._opts.receiverData.socketServer + ':443');

	socket.onopen = function(event){
		socket.send(JSON.stringify(fetcher._opts.receiverData.initiate));
		// Ensure our reconnect counter is reset
		fetcher._reconnectAttempts = 0;
	};

	socket.onmessage = function(event){
		fetcher.onMessage(event, fetcher);
	};
	
	socket.onclose = function(event){
		var interval = fetcher._reconnectConfig[fetcher._reconnectAttempts] || null;
		if (interval === null) {
			// We've exhausted all our retries
			alert('Error connecting to remote server. Check your internet connection and try reloading the page.');
			return;
		}

		// Attempt reconnect
		setTimeout($.proxy(fetcher.reconnectSocket, fetcher), interval);

		fetcher._reconnectAttempts++;
	};
};

/**
 * Wrapper for reestablishing a socket connection. We need this in addition to
 * setupSocket because we need to fetch fresh creds for our reconnect.
 */
SkyAwareDataFetcher.prototype.reconnectSocket = function(){
	var fetcher = this;
	$.ajax({
		dataType: 'json',
		url: 'data/receiver.json',
		data: window.location.search.substring(1),
		error: function(xhr){
			if (xhr.status == 403) {	
				// This means the user has no ADS-B sites, so send them elsewhere	
				window.location.href = '/adsb/';	
			} else {	
				alert('Error fetching ADS-B sites for display');	
			}	
		},
		success: function(receiverData){
			fetcher._opts.receiverData = receiverData;
			fetcher.setupSocket();
		}

	})
};

/**
 * Local storage for a list of known aircraft, keyed by hexid. We'll use this
 * to periodically generate a synthetic equivalent to aircraft.json on a feeder.
 */
SkyAwareDataFetcher.prototype._aircraft = {};

/**
 * Internal counter of received messages. Skyaware uses this for some message
 * rate reporting stuff.
 */
SkyAwareDataFetcher.prototype._messageCount = 0;

/**
 * Bundle up our aircraft list into a nice JSON package and ship it downstream
 * to skyaware
 */
SkyAwareDataFetcher.prototype.dispatchData = function(){
	/**
	 * First we do some housekeeping. We need to update the position/message
	 * ages, as well as get rid of any aircraft we haven't seen in a while.
	 */
	var now = Date.now() / 1000;
	Object.keys(this._aircraft).forEach(function(hex){
		var age = now - this._aircraft[hex]._clock;
		if (age > 300) {
			delete(this._aircraft[hex]);
		} else {
			// We're keeping this one around, so let's update the seen values
			this._aircraft[hex].seen = age;
			this._aircraft[hex].seen_pos = now - this._aircraft[hex]._posClock;
		}
	}, this);

	var fakeData = {
		now: now,
		messages: this._messageCount,
		aircraft: Object.values(this._aircraft)
	};

	this._opts.onNewData(fakeData);
};

// Socket message handler
SkyAwareDataFetcher.prototype.onMessage = function(event, fetcher){
	var data = JSON.parse(event.data);
		
	// Messages without a hexid are useless to us, so bail out in that case
	if (data.hexid === undefined) {
		return;
	}

	var aircraft = {
		hex: data.hexid,
		_clock: data.clock,
		// Fake RSSI to keep downstream code happy (we don't get this from grackle)
		rssi: 0
	};

	// Handle position clock field if present
	if ('position_age' in data) {
		aircraft._posClock = data.clock - data.position_age;
	}

	if ('mlat' in data && 'lat' in data && 'lon' in data) {
		aircraft['mlat'] = ['lat','lon']
	}

	Object.keys(fetcher._dataMap).forEach(function(inputKey){
		if (inputKey in data) {
			var map = fetcher._dataMap[inputKey]
			// Get the translated field name to use for output data if applicable
			var outputKey = inputKey;
			if (map !== null && 'outputKey' in map) {
				outputKey = map.outputKey;
			}

			// Typecast the value if necessary
			var datum = data[inputKey];
			if (map !== null && 'cast' in map) {
				switch (map.cast) {
					case 'number':
						datum = Number(datum);
						break;
					case 'array':
						datum = datum.split(' ');
						break;
				}
			}

			// And now we can write it to our object
			aircraft[outputKey] = datum;
		}
	});

	// Now update our aircraft data list
	if (!(data.hexid in fetcher._aircraft)) {
		// We haven't seen this one before, so add the object verbatim
		fetcher._aircraft[data.hexid] = aircraft;
		// Start the message counter at 1
		fetcher._aircraft[data.hexid].messages = 1;
	} else {
		/**
		 * We're updating a known aircraft; do this for individual keys rather
		 * than replacing the entire object; this allows data that aren't sent
		 * frequently to persist in the UI (example: ADS-B category)
		 */
		Object.keys(aircraft).forEach(function(key){
			fetcher._aircraft[data.hexid][key] = aircraft[key];
		});
		// Also increment the message counter on this aircraft
		fetcher._aircraft[data.hexid].messages++;
	}

	// And increment our message counter
	this._messageCount++;
};

/**
 * This big SOB defines how we map data from grackle into a format that skyaware
 * likes. The key for each item is the key found in the grackle data. The value
 * defines how the mapping will happen. If the value is null, the same key will
 * be used in the output data, and the value from grackle will be used as found.
 * Alternatively, the value can be an object, with the following optional keys:
 * 	- cast: defines how to cast or alter the datum for consumption. Everything
 * comes from grackle as a string, so this is mostly used to cast numbers where
 * appropriate.
 * 	- outputKey: if we need to use a different key for the skyaware output, this
 * is what should be used.
 * 
 */
SkyAwareDataFetcher.prototype._dataMap = {
	ident: {
		outputKey: 'flight'
	},
	// Location fields
	lat: {
		cast: 'number'
	},
	lon: {
		cast: 'number'
	},
	// Speed fields
	gs: {
		cast: 'number'
	},
	speed_ias: {
		cast: 'number',
		outputKey: 'ias'
	},
	speed_tas: {
		cast: 'number',
		outputKey: 'tas'
	},
	mach: {
		cast: 'number'
	},
	// Altitude-related fields
	alt: {
		cast: 'number',
		outputKey: 'alt_baro'
	},
	vrate: {
		cast: 'number',
		outputKey: 'baro_rate'
	},
	alt_gnss: {
		cast: 'number',
		outputKey: 'alt_geom'
	},
	vrate_geom: {
		cast: 'number',
		outputKey: 'geom_rate'
	},
	// Direction
	heading: {
		cast: 'number',
		outputKey: 'track'
	},
	heading_magnetic: {
		cast: 'number',
		outputKey: 'mag_heading'
	},
	heading_true: {
		cast: 'number',
		outputKey: 'true_heading'
	},
	track_rate: {
		cast: 'number'
	},
	roll: {
		cast: 'number'
	},
	// Autopilot modes
	nav_alt_mcp: {
		cast: 'number',
		outputKey: 'nav_altitude_mcp'
	},
	nav_alt_fms: {
		cast: 'number',
		outputKey: 'nav_altitude_fms'
	},
	nav_heading: {
		cast: 'number'
	},
	nav_modes: {
		cast: 'array'
	},
	nav_qnh: {
		cast: 'number'
	},
	// General info
	adsb_version: {
		outputKey: 'version'
	},
	adsb_category: {
		outputKey: 'category'
	},
	squawk: {},
	// Accuracy stuff
	nac_p: {
		cast: 'number'
	},
	nac_v: {
		cast: 'number'
	},
	pos_rc: {
		cast: 'number',
		outputKey: 'rc'
	},
	sil: {
		cast: 'number'
	},
	sil_type: {},
	nic_baro: {
		cast: 'number'
	}
};

module.exports = {
	SkyAwareDataFetcher: SkyAwareDataFetcher
}