const path = require('path');
const webpack = require('webpack');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const HtmlMinimizerPlugin = require("html-minimizer-webpack-plugin");
const TerserPlugin = require('terser-webpack-plugin');

module.exports = {
  resolve: {
    fallback: {
      "os": false,
      "url": false,
      "http": false,
      "https": false,
      "console": false,	 
      "tty": false,
      "vm": false,
      "fs": false
    }
  },
  mode: 'development',
  devtool: 'source-map',
	//TODO: add jquery/ol libraries to bundle
  entry: {
    bundle: [
      path.resolve(__dirname, 'entry.js')
    ],
  },
  output: {
    filename: '[name].[contenthash].js',
    path: path.resolve(__dirname, 'dist/'),
    libraryTarget: 'var',
    library: 'EntryPoint',
    // libraryExport: 'default',
    clean: true
  },
  module: {
    rules: [
      {
        test: /\.css$/i,
        use: ['style-loader', 'css-loader']
      },
      {
        test: /\.html$/i,
        loader: "html-loader"
      },
      // {
      //   test: /\.(png|svg|jpg)$/i,
      //   loader: 'file-loader'
      // },
    ]
  },
  optimization: {
    minimize: true,
    minimizer: [
      new TerserPlugin({ parallel: true }),
      new HtmlMinimizerPlugin()
    ]
  },
  plugins: [
    new HtmlWebpackPlugin({
      template: './template.html',
      filename: 'index.html',
      inject: 'head',
      scriptLoading: 'blocking'
    })
  ]
};
