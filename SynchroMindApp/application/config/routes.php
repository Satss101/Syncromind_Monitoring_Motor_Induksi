<?php
defined('BASEPATH') OR exit('No direct script access allowed');

/*
| -------------------------------------------------------------------------
| URI ROUTING
| -------------------------------------------------------------------------
| This file lets you re-map URI requests to specific controller functions.
|
| Typically there is a one-to-one relationship between a URL string
| and its corresponding controller class/method. The segments in a
| URL normally follow this pattern:
|
|	example.com/class/method/id/
|
| In some instances, however, you may want to remap this relationship
| so that a different class/function is called than the one
| corresponding to the URL.
|
| Please see the user guide for complete details:
|
|	https://codeigniter.com/userguide3/general/routing.html
|
| -------------------------------------------------------------------------
| RESERVED ROUTES
| -------------------------------------------------------------------------
|
| There are three reserved routes:
|
|	$route['default_controller'] = 'welcome';
|
| This route indicates which controller class should be loaded if the
| URI contains no data. In the above example, the "welcome" class
| would be loaded.
|
|	$route['404_override'] = 'errors/page_missing';
|
| This route will tell the Router which controller/method to use if those
| provided in the URL cannot be matched to a valid route.
|
|	$route['translate_uri_dashes'] = FALSE;
|
| This is not exactly a route, but allows you to automatically route
| controller and method names that contain dashes. '-' isn't a valid
| class or method name character, so it requires translation.
| When you set this option to TRUE, it will replace ALL dashes in the
| controller and method URI segments.
|
| Examples:	my-controller/index	-> my_controller/index
|		my-controller/my-method	-> my_controller/my_method
*/
$route['default_controller'] = 'Auth';
$route['404_override']       = 'Errors/page_404';
$route['translate_uri_dashes'] = FALSE;

// --- Auth ---
$route['login']             = 'Auth/login';
$route['login/process'] = 'Auth/do_login'; 
$route['logout']            = 'Auth/logout';
$route['forgot-password']   = 'Auth/forgot_password';
$route['reset-password/(:segment)'] = 'Auth/reset_password/$1';

// --- Dashboard ---
$route['dashboard']         = 'Dashboard/index';

// --- User Management ---
$route['users']             = 'Users/index';
$route['users/create']      = 'Users/create';
$route['users/store']       = 'Users/store';
$route['users/edit/(:num)'] = 'Users/edit/$1';
$route['users/update/(:num)'] = 'Users/update/$1';
$route['users/delete/(:num)'] = 'Users/delete/$1';
$route['users/toggle/(:num)'] = 'Users/toggle_active/$1';
$route['profile']           = 'Users/profile';
$route['profile/update']    = 'Users/update_profile';
$route['profile/password']  = 'Users/change_password';

// --- API USER ---
$route['api/users'] = 'api/User_api/index';
$route['api/users/(:num)'] = 'api/User_api/detail/$1';

// --- API DATA SENSOR ---
$route['api/sensor'] = 'api/Sensor_api/index';