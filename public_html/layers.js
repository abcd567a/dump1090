// -*- mode: javascript; indent-tabs-mode: nil; c-basic-offset: 8 -*-
"use strict";

// Base layers configuration

function createBaseLayers() {
        var layers = [];

        var world = [];
        var us = [];

        world.push(new ol.layer.Tile({
                source: new ol.source.OSM(),
                name: 'osm',
                title: 'OpenStreetMap',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.XYZ({
                        "url" : "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
	                "attributions" : "Tiles © Esri - Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community",
                }),
	        name: 'esri_satellite',
                title: 'ESRI Satellite',
                type: 'base',
	}));

        world.push(new ol.layer.Tile({
                source: new ol.source.XYZ({
                        "url" : "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}",
                        "attributions" : "Tiles © Esri - Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community",
                }),
                name: 'esri_topo',
                title: 'ESRI Topographic',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.XYZ({
			"url" : "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}",
                        "attributions" : "Tiles © Esri - Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community",
                }),
                name: 'esri_street',
                title: 'ESRI Street',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.XYZ({
			"url" : "http://tiles.wmflabs.org/bw-mapnik/{z}/{x}/{y}.png",
			"attributions" : "© <a href=\"https://www.openstreetmap.org/copyright\" target=\"_blank\">OpenStreetMap</a>  contributors."
                }),
                name: 'osm_blackwhite',
                title: 'OSM Black and White',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.OSM({
                        "url" : "https://{a-z}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png",
                        "attributions" : 'Courtesy of <a href="https://carto.com">CARTO.com</a>'
                               + ' using data by <a href="http://openstreetmap.org">OpenStreetMap</a>, under <a href="http://www.openstreetmap.org/copyright">ODbL</a>.',
                }),
                name: 'carto_dark_all',
                title: 'CARTO.COM Dark',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.OSM({
                        "url" : "https://{a-z}.basemaps.cartocdn.com/dark_nolabels/{z}/{x}/{y}.png",
                        "attributions" : 'Courtesy of <a href="https://carto.com">CARTO.com</a>'
                               + ' using data by <a href="http://openstreetmap.org">OpenStreetMap</a>, under <a href="http://www.openstreetmap.org/copyright">ODbL</a>.',
                }),
                name: 'carto_dark_nolabels',
                title: 'CARTO.com Dark (No Labels)',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.OSM({
                        "url" : "https://{a-z}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png",
                        "attributions" : 'Courtesy of <a href="https://carto.com">CARTO.com</a>'
                               + ' using data by <a href="http://openstreetmap.org">OpenStreetMap</a>, under <a href="http://www.openstreetmap.org/copyright">ODbL</a>.',
                }),
                name: 'carto_light_all',
                title: 'CARTO.com Light',
                type: 'base',
        }));

        world.push(new ol.layer.Tile({
                source: new ol.source.OSM({
                        "url" : "https://{a-z}.basemaps.cartocdn.com/light_nolabels/{z}/{x}/{y}.png",
                        "attributions" : 'Courtesy of <a href="https://carto.com">CARTO.com</a>'
                               + ' using data by <a href="http://openstreetmap.org">OpenStreetMap</a>, under <a href="http://www.openstreetmap.org/copyright">ODbL</a>.',
                }),
                name: 'carto_light_nolabels',
                title: 'CARTO.com Light (No Labels)',
                type: 'base',
        }));


        if (BingMapsAPIKey) {
                world.push(new ol.layer.Tile({
                        source: new ol.source.BingMaps({
                                key: BingMapsAPIKey,
                                imagerySet: 'Aerial'
                        }),
                        name: 'bing_aerial',
                        title: 'Bing Aerial',
                        type: 'base',
                }));
                world.push(new ol.layer.Tile({
                        source: new ol.source.BingMaps({
                                key: BingMapsAPIKey,
                                imagerySet: 'Road'
                        }),
                        name: 'bing_roads',
                        title: 'Bing Roads',
                        type: 'base',
                }));
        }

        if (ChartBundleLayers) {
                var chartbundleTypes = {
                        sec: "Sectional Charts",
                        tac: "Terminal Area Charts",
                        hel: "Helicopter Charts",
                        enrl: "IFR Enroute Low Charts",
                        enra: "IFR Area Charts",
                        enrh: "IFR Enroute High Charts"
                };

                for (var type in chartbundleTypes) {
                        us.push(new ol.layer.Tile({
                                source: new ol.source.TileWMS({
                                        url: 'http://wms.chartbundle.com/wms',
                                        params: {LAYERS: type},
                                        projection: 'EPSG:3857',
                                        attributions: 'Tiles courtesy of <a href="http://www.chartbundle.com/">ChartBundle</a>'
                                }),
                                name: 'chartbundle_' + type,
                                title: chartbundleTypes[type],
                                type: 'base',
                                group: 'chartbundle'}));
                }
        }

        var nexrad = new ol.layer.Tile({
                name: 'nexrad',
                title: 'NEXRAD',
                type: 'overlay',
                opacity: 0.5,
                visible: false
        });
        us.push(nexrad);

        var refreshNexrad = function() {
                // re-build the source to force a refresh of the nexrad tiles
                var now = new Date().getTime();
                nexrad.setSource(new ol.source.XYZ({
                        url : 'http://mesonet{1-3}.agron.iastate.edu/cache/tile.py/1.0.0/nexrad-n0q-900913/{z}/{x}/{y}.png?_=' + now,
                        attributions: 'NEXRAD courtesy of <a href="http://mesonet.agron.iastate.edu/">IEM</a>'
                }));
        };

        refreshNexrad();
        window.setInterval(refreshNexrad, 5 * 60000);

        if (world.length > 0) {
                layers.push(new ol.layer.Group({
                        name: 'world',
                        title: 'Worldwide',
                        layers: world
                }));
        }

        if (us.length > 0) {
                layers.push(new ol.layer.Group({
                        name: 'us',
                        title: 'US',
                        layers: us
                }));
        }

        return layers;
}
