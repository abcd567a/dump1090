/**
 * Abstracted data fetcher class for SkyAware
 */

var SkyAwareDataFetcher = function(opts){
	this.init(opts);
};

// Constructor
SkyAwareDataFetcher.prototype.init = function(opts){
	// Get our callbacks from the options
	this.onNewData = opts.onNewData;
	this.onDataError = opts.onDataError;
	
	// Start the data-fetching interval
	window.setInterval(this.fetchData, opts.refreshInterval);

	// And kick off one fetch immediately.
	this.fetchData();
};

// Possibly stores a pending ajax request for fresh data
SkyAwareDataFetcher.prototype.fetchPending = null

// Fetch fresh data and fire callbacks as appropriate
SkyAwareDataFetcher.prototype.fetchData = function(){
	if (FetchPending !== null && FetchPending.state() == 'pending') {
		// don't double up on fetches, let the last one resolve
		return;
	}

	FetchPending = $.ajax({
		url: 'data/aircraft.json',
		timeout: 5000,
		cache: false,
		dataType: 'json'
	});
	
	FetchPending.done(this.onNewData);
	FetchPending.fail($.proxy(function(jqxhr, status, error) {
		this.onDataError("AJAX call failed (" + status + (error ? (": " + error) : "") + "). Maybe dump1090 is no longer running?");
	}, this));
};