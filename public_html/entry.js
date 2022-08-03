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
import './data.js';
import './script.js';

// support legacy code
import $ from 'jquery';
global.$ = $;

// declare globally accessible variables
import Control from 'ol/control/Control';
import { Observable } from 'ol';
global.ol.control.Control = Control;
global.ol.Observable = Observable;

// export functions, classes, etc. from JS assets
export { initialize } from './script';
export { createBaseLayers } from './layers';
export { get_unit_label, format_distance_long } from './formatter';
export { SkyAwareDataFetcher } from './data';

// TODO: try to get ol library to work
// export { Observable, Collection } from 'ol';
// export { Control } from 'ol/control';