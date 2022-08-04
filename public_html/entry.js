// This file exists as an entry point for webpack to bundle the assets for SkyAware Anywhere

// Add our styling
import './style.css';
import './ol/v6.3.1/ol.css';
import './ol/ol-layerswitcher.css';
import './noUiSlider/distribute/nouislider.css';
// import './jquery/jquery-ui-1.11.4-smoothness.css';

// library imports
import './jquery/jquery-3.0.0.min.js';
import './jquery/jquery-ui-1.11.4.min.js'
import './jquery/plugins/jquery.validate.min.js';
import './ol/v6.3.1/ol.js';
import './ol/ol-layerswitcher.js';
import './noUiSlider/distribute/nouislider.min.js';

// JavaScript assets
import './config.js';
import './markers.js';
import './dbloader.js';
import './registrations.js';
import './planeObject.js';
import './formatter.js';
import './flags.js';
import './layers.js';
import './script.js';
import './data.js';

// support legacy code
import $ from 'jquery';
global.$ = $;

// declare globally accessible variables
// Please note I had to refactor code to make use of globally accessible variables. ex: ZoomLvl = 'blahblah' ==> EntryPoint.ZoomLvl = 'blahblah' and so on
import Control from 'ol/control/Control';
import { Observable } from 'ol';
global.ol.control.Control = Control;
global.ol.Observable = Observable;
global.receiverData;
global.ZoomLvl;
global.AircraftLabels;

// export functions, classes, etc. from JS assets
export { initialize, sortByICAO, sortByCountry, sortByFlight, sortBySquawk, sortByAltitude, sortBySpeed, sortByDistance, sortByTrack, sortByMsgs,
	sortBySeen,	sortByRegistration, sortByAircraftType, sortByVerticalRate, sortByLatitude, sortByLongitude, sortByDataSource,
	SpecialSquawks, AircraftLabels, OLMap, PlaneIconFeatures, PlaneTrailFeatures, SelectedPlane, SelectedAllPlanes } from './script';
export { createBaseLayers } from './layers';
export { get_unit_label, format_distance_long, format_distance_brief, format_distance_short, format_altitude_brief, format_altitude_long, 
	format_speed_brief, format_speed_long, format_vert_rate_brief, format_vert_rate_long, format_nac_p, 
	format_nac_v, format_data_source, format_onground, format_latlng, format_track_brief, format_track_long } from './formatter';
export { SkyAwareDataFetcher } from './data';
export { PlaneObject } from './planeObject';
export { findICAORange } from './flags';
export { getAircraftData } from './dbloader';
export { getBaseMarker, svgPathToURI } from './markers';

// TODO: try to get ol library to work
// export { Observable, Collection } from 'ol';
// export { Control } from 'ol/control';