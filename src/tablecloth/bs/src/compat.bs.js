// Generated by BUCKLESCRIPT, PLEASE EDIT WITH CARE
'use strict';

var $$String = require("bs-platform/lib/js/string.js");

var toLower = $$String.lowercase_ascii;

var toUpper = $$String.uppercase_ascii;

var uncapitalize = $$String.uncapitalize_ascii;

var capitalize = $$String.capitalize_ascii;

function isCapitalized(s) {
  return s === $$String.capitalize_ascii(s);
}

var $$String$1 = {
  toLower: toLower,
  toUpper: toUpper,
  uncapitalize: uncapitalize,
  capitalize: capitalize,
  isCapitalized: isCapitalized
};

exports.$$String = $$String$1;
/* No side effect */
