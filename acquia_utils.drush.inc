<?php
/**
 * @file
 * Acquia utils drush commands.
 */

/**
 * Acquia load balancer type
 */
define('ACQUIA_SERVER_TYPE_BALANCER', 'bal');

/**
 * Acquia database server type
 */
define('ACQUIA_SERVER_TYPE_DATABSE', 'ded');

/**
 * Acquia web server type
 */
define('ACQUIA_SERVER_TYPE_WEB', 'web');


/**
 * Implements hook_drush_command().
 */
function acquia_utils_drush_command() {
  $cmds = array();
  $common_options = _acquia_utils_common_options();

  // Add Acquia cloud API credentials.
  $cmds['au-add-credentials'] = array(
    'callback' => 'acquia_utils_exec_command',
    'callback arguments' => array('add_credentials'),
    'description' => 'Add Acquia API key & username to the store',
    'options' => $common_options + array(
      // More options here.
    ),
    'aliases' => array('aucred'),
    'examples' => array(
      'au-add-credentials --email=myemail@test.com --key=apiKey' => 'Add Acquia Cloud API credentials to the store',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  // Clear Acquia Varnish cache.
  $cmds['au-purge-varnish'] = array(
    'callback' => 'acquia_utils_exec_command',
    'callback arguments' => array('purge_varnish', TRUE),
    'description' => 'Purge Varnish cache for certain paths on Acquia server',
    'options' => $common_options + array(
      'paths' => array(
        'description' => 'The paths to be purged',
        'example_value' => 'path1,path2,path3',
        'required' => TRUE,
      ),
    ),
    'aliases' => array('aupv'),
    'examples' => array(
      'au-purge-varnish --email=email --key=apiKey --sitename=site --paths=paths' => 'Purge Varnish cache for @paths on @site',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  // Show live log stream.
  $cmds['au-logstream'] = array(
    'callback' => 'acquia_utils_exec_command',
    'callback arguments' => array('show_logstream', TRUE),
    'description' => 'Display live logging information of Acquia server',
    'options' => $common_options + array(
      'env' => array(
        'description' => 'Acquia server environment',
        'example_value' => 'dev|test|prod',
      ),
      'types' => array(
        'description' => 'Types of log to be displayed',
        'example_value' => 'apache-error,php-error,drupal-request,drupal-watchdog,varnish-request',
      ),
    ),
    'aliases' => array('logstream'),
    'examples' => array(
      'au-logstream --email=email --key=apiKey --sitename=site --env=prod --types=varnish-request' => 'Show live Varnish request log for @site on environment @prod',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  return $cmds;
}


/**
 * Get definitions for common options.
 */
function _acquia_utils_common_options() {
  $options = array(
    'sitename' => array(
      'description' => 'Machine name of the Acquia site (subscription)',
      'example_value' => 'mysite',
    ),
    'email' => array(
      'description' => 'Acquia Network account email address',
      'example_value' => 'youremail@acquia.com',
    ),
    'key' => array(
      'description' => 'Private Acquia Cloud API key for your account',
      'example_value' => 'apiKey',
    ),
    'verbose' => array(
      'description' => 'Print out debug information',
      'example_value' => 'true|false',
    ),
  );

  return $options;
}


/**
 * Generic function to exec a command.
 */
function acquia_utils_exec_command($command, $login_required = FALSE) {
  $options  = _acquia_utils_get_common_options();

  // If Acquia Cloud API authentication is required.
  if ($login_required) {
    $config   = _acquia_utils_load_config();
    $sitename = $options['sitename'];

    if (!$sitename) {
      // Try to get site name if --email & --key are provided.
      if ($options['email'] && $options['key'] && $config) {
        foreach ($config as $site_name => $creds) {
          if ($creds['email'] == $options['email'] && $creds['key'] == $options['key'] && $site_name != 'all') {
            $sitename = $site_name;
            break;
          }
        }
      }
    }

    // All APIs with login required need --sitename to be present.
    if (!$sitename) {
      return drush_set_error(dt('No site is provided. Use --sitename to specify the site name.'));
    }

    // Make it available for all commands.
    $options['sitename'] = $sitename;

    $credentials = array();

    // Override the cached credentials.
    if ($options['email'] && $options['key']) {
      $credentials = array(
        'email' => $options['email'],
        'key'   => $options['key'],
      );
    }
    else {
      // If not, try to load it from config file.
      if (isset($config[$sitename])) {
        $credentials = $config[$sitename];
      }
      elseif (isset($config['all'])) {
        $credentials = $config['all'];
      }
    }

    // No valid credentials found, no need to proceed.
    if (empty($credentials)) {
      return drush_set_error(dt('Command requires API login. Use --email and --key to provide Acquia Cloud Credentials'));
    }

    $credentials = (array) $credentials;

    // Login fails for whatever reason.
    if (!acquia_utils_ac_api_login($sitename, $credentials)) {
      return drush_set_error(dt('Acquia Cloud API login fails. Please check your credentials and/or site name'));
    }
    drush_print('Acquia cloud API authenticated');
  }

  $drush_cmd = "drush_acquia_utils_{$command}";

  if (function_exists($drush_cmd)) {
    call_user_func($drush_cmd, $options);
  }
}

/**
 * Get the path to the config file.
 */
function _acquia_utils_get_config_file() {
  $home = drush_server_home();
  $config_file = '.acquia_utils.conf';

  return $home . '/.drush/' . $config_file;
}


/**
 * Get common options passed to drush commands.
 */
function _acquia_utils_get_common_options() {
  $options = _acquia_utils_common_options();
  $retval  = array();

  foreach ($options as $opt_name => $opt_info) {
    $opt = drush_get_option($opt_name, FALSE);
    $retval[$opt_name] = $opt;
  }

  return $retval;
}


/**
 * Load config from the config file.
 */
function _acquia_utils_load_config() {
  $config_file = _acquia_utils_get_config_file();
  $config  = array();
  $content = @file_get_contents($config_file);

  if ($content) {
    $config = (array) json_decode($content);
  }

  return $config;
}


/**
 * Save config to config file.
 */
function _acquia_utils_save_config($config) {
  $config_file = _acquia_utils_get_config_file();
  $config = json_encode($config);

  return @file_put_contents($config_file, $config);
}


/**
 * Callback for drush command au-add-credentials.
 */
function drush_acquia_utils_add_credentials($options) {
  if (!$options['email'] || !$options['key']) {
    return drush_set_error(dt('Credentials missing. Use --email and --key to specify them'));
  }

  $config = _acquia_utils_load_config();
  $sitename = $options['sitename'] ? $options['sitename'] : 'all';

  // If credentials for the sitename is exsiting, ask user to confirm
  // overriding.
  if ($options['sitename'] && $config[$sitename]) {
    if (!drush_confirm(dt('The credentials for @site is existing. Override with new one?', array('@site' => $sitename)))) {
      return;
    }
  }

  // Add new config to the whole set.
  $config[$sitename] = array(
    'email' => $options['email'],
    'key'   => $options['key'],
  );

  if (_acquia_utils_save_config($config)) {
    drush_print(dt('Credentials saved to: @file', array('@file' => _acquia_utils_get_config_file())));
  }
  else {
    drush_set_error(dt('Failed to save credentials'));
  }
}


/**
 * Callback function for au-purge-varnish command.
 */
function drush_acquia_utils_purge_varnish($options) {
  $paths = drush_get_option_list('paths', FALSE);
  if (empty($paths)) {
    return drush_set_error(dt('Invalid paths. Use --paths to specify the paths to be purged.'));
  }

  $sitename = $options['sitename'];
  $verbose  = $options['verbose'];

  // Loop through the paths and try to guess the domains.
  foreach ($paths as $path) {
    $url_info = parse_url($path);
    $path = '/' . ltrim($url_info['path'], '/');

    // If the path is the full URL including the hostname then try to purge it
    // directly.
    if ($url_info['host']) {
      _acquia_utils_purge_varnish_path($sitename, $url_info['host'], $path, $verbose);
    }
    else {
      _acquia_utils_purge_varnish_path_all_domains($sitename, $path, $verbose);
    }
  }
}


/**
 * Purge Varnish cache for a path of a certain domain.
 */
function _acquia_utils_purge_varnish_path($sitename, $domain, $path, $verbose = FALSE) {
  $servers = _acquia_utils_get_server_list($sitename, ACQUIA_SERVER_TYPE_BALANCER);

  // If no load balancer is found, try to ghet load balancer IP addresses.
  if (empty($servers)) {
    $servers = _acquia_utils_get_load_balancer_ips($sitename);
  }

  // Purge cache for each load balancer.
  if (!empty($servers)) {
    foreach ($servers as $load_balancer) {
      _acquia_utils_curl_purge_send($sitename, $domain, $load_balancer, $path, $verbose);
    }
  }

  // In the worst case, try the simple purging.
  else {
    _acquia_utils_curl_purge_send($sitename, $domain, $domain, $path, $verbose);
  }
}


/**
 * Send curl request for purge action.
 */
function _acquia_utils_curl_purge_send($sitename, $domain, $server, $path, $verbose = FALSE) {
  $headers = array(
    "X-Acquia-Purge: $sitename",
    "Host: $domain",
    "Accept-Encoding: gzip",
  );

  $url = "http://{$server}{$path}";

  drush_print(dt('Purging Varnish cache for: @path', array('@path' => $url)));

  $ch = curl_init();
  curl_setopt($ch, CURLOPT_URL, $url);
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE);
  curl_setopt($ch, CURLOPT_HTTPHEADER, $headers);
  curl_setopt($ch, CURLOPT_HEADER, TRUE);
  curl_setopt($ch, CURLOPT_CUSTOMREQUEST, 'PURGE');

  if ($verbose) {
    curl_setopt($ch, CURLOPT_VERBOSE, TRUE);
  }

  $output = curl_exec($ch);
  curl_close($ch);

  drush_print($output);
}


/**
 * Get the list of load balancer IPs for a given site.
 */
function _acquia_utils_get_load_balancer_ips($sitename) {
  $cmd = "drush @{$sitename}.prod vget reverse_proxies";
  $code = 0;
  $output = array();
  $ips = array();

  @exec($cmd, $output, $code);

  if (!$code && !empty($output)) {
    foreach ($output as $line) {
      if (preg_match_all('/[0-9]{1,3}\.[0-9]{1,3]\.[0-9]{1,3}\.[0-9]{1,3}/', $line, $matches)) {
        $ips[] = $matches[0];
      }
    }
  }

  return $ips;
}


/**
 * Purge Varnish cache for all domains.
 */
function _acquia_utils_purge_varnish_path_all_domains($sitename, $path, $verbose = FALSE) {
  $domains = _acquia_utils_get_domain_list($sitename);

  if (!$domains) {
    return drush_set_error(dt('Failed to get domain list. Please check your credentials'));
  }

  if (count($domains) == 1) {
    _acquia_utils_purge_varnish_path($sitename, $domains[0], $path);
  }
  // Lets use decide what domain to purge in case we have more than one.
  else {
    drush_print(dt('Purging Varnish cache for path: "@path" on site "@site"', array('@path' => $path, '@site' => $sitename)));

    $prompt = array(
      "\t0:\tCancel",
    );

    for ($i = 0, $c = count($domains); $i < $c; $i++) {
      $prompt[] = "\t" . ($i + 1) . ":\t{$domains[$i]}";
    }

    drush_print(implode("\n", $prompt));

    $indices = drush_prompt(dt('Please select the domains (numbers separated by commas) you want to purge or enter 0 to cancel'), 0, FALSE);

    // Invalid domain or user wants to cancel.
    if ($indices == "0" || empty($indices)) {
      return drush_set_error(dt('Invalid domain selected. Abort now'));
    }

    // Purge the path for all the selected domains.
    $indices = explode(',', $indices);

    foreach ($indices as $index) {
      $index = intval($index);

      if ($index && isset($domains[$index - 1])) {
        _acquia_utils_purge_varnish_path($sitename, $domains[$index - 1], $path, $verbose);
      }
    }
  }
}


/**
 * Get list of domains for a site.
 */
function _acquia_utils_get_domain_list($sitename) {
  static $domains;

  if (isset($domains)) {
    return $domains;
  }

  $retval = _acquia_utils_exec_drush_command($sitename, 'ac-domain-list');

  if ($retval['code'] || empty($retval['output'])) {
    return FALSE;
  }

  $output = $retval['output'];
  $domains = array();

  // Strip unneeded texts to get the domain names.
  foreach ($output as $line) {
    $parts = explode(':', $line);
    if (count($parts) == 2) {
      $domains[] = trim($parts[1]);
    }
  }

  return $domains;
}


/**
 * Get list of server names for a site.
 */
function _acquia_utils_get_server_list($sitename, $server_type) {
  static $servers;

  if (isset($servers[$server_type])) {
    return $servers[$server_type];
  }

  $retval = _acquia_utils_exec_drush_command($sitename, 'ac-server-list');

  if ($retval['code'] || empty($retval['output'])) {
    return FALSE;
  }

  $regex = "/{$server_type}\-[0-9]+(\.prod\.hosting\.acquia\.com)/";
  foreach ($retval['output'] as $line) {
    if (preg_match($regex, $line, $matches)) {
      $servers[$server_type][] = $matches[0];
    }
  }

  return isset($servers[$server_type]) ? $servers[$server_type] : FALSE;
}


/**
 * Exec a drush command for an alias.
 */
function _acquia_utils_exec_drush_command($sitename, $command, $params = array()) {
  $options = array();

  foreach ($params as $key => $val) {
    $options[] = "--{$key}={$val}";
  }

  $options = !empty($options) ? implode(' ', $options) : '';
  $alias = "{$sitename}.prod";

  $command = "drush @{$alias} {$command} {$options}";
  $output  = array();
  $code    = 0;

  @exec($command, $output, $code);

  return array(
    'code'   => $code,
    'output' => $output,
  );
}


/**
 * Login to Acquia Cloud API.
 */
function acquia_utils_ac_api_login($sitename, $credentials) {
  static $logged_in;

  if (isset($logged_in)) {
    return $logged_in;
  }

  $options = array(
    'endpoint' => 'https://cloudapi.acquia.com/v1',
    'email' => $credentials['email'],
    'key' => $credentials['key'],
  );

  $retval = _acquia_utils_exec_drush_command($sitename, 'ac-api-login', $options);

  // No output means success.
  $logged_in = empty($retval['output']) && ($retval['code'] == 0);

  return $logged_in;
}


/**
 * Get all avaiable log stream types.
 */
function _acquia_utils_get_logstream_types() {
  return array(
    'apache-request',
    'apache-error',
    'php-error',
    'drupal-request',
    'drupal-watchdog',
    'varnish-request',
  );
}


/**
 * Show live log stream on Acquia server.
 */
function drush_acquia_utils_show_logstream($options) {
  // Pre-conditions check.
  if (!_acquia_utils_check_logstream_install()) {
    return;
  }

  $all_types = _acquia_utils_get_logstream_types();
  $types = drush_get_option_list('types', $all_types);
  $types = implode(' ', $types);

  $sitename = $options['sitename'];
  $environment = drush_get_option('env', 'prod');

  drush_print(dt('Logstream for site "@site" on environment "@env"', array('@site' => $sitename, '@env' => $environment)));

  // First, try with devcloud sbuscription type.
  $command = "logstream tail devcloud:{$sitename} {$environment} --types={$types}";
  @system($command, $exit_code);

  // If we fail, there is possibility that we used a wrong site name (e.g,
  // devcloud vs prod) so we just retry with the other one.
  if ($exit_code) {
    $command = "logstream tail prod:{$sitename} {$environment} --types={$types}";
    @system($command, $exit_code);
  }
}


/**
 * Check if logstream is installed and install it if not.
 */
function _acquia_utils_check_logstream_install() {
  $exit_code = 0;
  $output = array();

  @exec('which gem', $output, $exit_code);

  // Ruby gem is not installed. Prompt user to install it.
  if ($exit_code && empty($output)) {
    drush_set_error(dt('Ruby Gem is required to run this command. Please install Ruby first'));
    return FALSE;
  }

  // Check if logstream is installed.
  @exec('which logstream', $output, $exit_code);

  if ($exit_code) {
    if (drush_confirm(dt('logstream gem is required to run this command. Do you want to install it?'))) {
      @exec('gem install logstream');
    }
    else {
      return FALSE;
    }
  }

  return TRUE;
}
