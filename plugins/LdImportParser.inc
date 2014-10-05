<?php

/**
 * @file
 * Class definition for Linked Data Import parser plugin.
 */

/**
 * Class definition for LdImportParser.
 */
class LdImportParser extends FeedsParser {

  /**
   * Implements FeedsParser::parse().
   */
  public function parse(FeedsSource $source, FeedsFetcherResult $fetcher_result) {
    $uri_list = array();

    // Ideally fetcher is designed to provide a list of URIs. If not, we still
    // try and support other fetchers, expecting URIs on separate lines.
    if (method_exists($fetcher_result, 'getList')) {
      $uri_list = $fetcher_result->getList();
    }
    else {
      $raw = $fetcher_result->getRaw();
      foreach (explode("\n", $raw) as $uri) {
        $uri = trim($uri);
        if ($uri && valid_url($uri, TRUE)) {
          $uri_list[$uri] = $uri;
        }
      }
    }

    $uri_list = array_values($uri_list);

    $state = $source->state(FEEDS_PARSE);
    $state->total = count($uri_list);
    $state->parsed = 0;
    $state->retry = array();

    return new LdimportParserResult($uri_list, $this, $source);
  }

  /**
   * Pull the next item off the list to give to the processor.
   *
   * This is called by LdimportParserResult::shiftItem(). By handing off items
   * one at a time we reduce the chances of items getting lost when we hit
   * maximum execution times.
   *
   * Because we batch at the parser level, items handed to the processor are
   * considered "done" even if the processor fails to finish.
   */
  public function getItem($uri_list, $source) {
    $state = $source->state(FEEDS_PARSE);

    $arc_config = array(
      'reader_timeout' => $this->config['timeout'],
    );
    $arc = ($this->config['use_caching']) ? ldimport_get_arc($arc_config) : ARC2::getResource($arc_config);

    $start = ($state->pointer) ? $state->pointer : 0;
    $limit = $this->config['batch_limit'];
    $parsed = $state->parsed;

    // Run through URIs until we hit the limit or finish the list.
    for ($i = $start; $i < count($uri_list) && $parsed < $limit; $i++) {

      $attempts = !empty($this->config['retries']) ? $this->config['retries'] : 1;

      while ($attempts >= 0) {
        $uri = $uri_list[$i];
        $item = new LdImportItem($uri, $arc, $GLOBALS['language']->language);

        if ($attempts && (empty($this->config['pre_fetch']) || $item->hasData())) {
          $state->created++;
          $state->parsed++;
          $state->pointer = $i + 1;
          $state->progress($state->total, $state->created + $state->failed);
          return $item;
        }
        elseif (!$attempts) {
          if ($errors = $item->arc->getErrors()) {
            $message = t('Failed importing %uri after %n attempts. The following errors were encountered: @errors', array('%uri' => $uri, '@errors' => implode(' | ', $errors), '%n' => $this->config['retries']));
          }
          else {
            $message = t('Failed importing %uri after %n attempts. No data was returned.', array('%uri' => $uri, '%n' => $this->config['retries']));
          }
          $source->log('import', $message, array(), WATCHDOG_WARNING);
          $state->failed++;
          $state->pointer = $i + 1;
        }

        $attempts--;
      }
    }

    return NULL;
  }

  /**
   * Overrides FeedsParser::getSourceElement().
   */
  public function getSourceElement(FeedsSource $source, FeedsParserResult $result, $element_key) {
    $item = $result->currentItem();

    $predicates = $this->getPredicates($element_key);

    $value = '';

    if ($values = $this->extractPropertyValues($item, $predicates)) {
      $value = (count($values) == 1) ? reset($values) : $values;
    }

    return $value;
  }

  /**
   * Recursively retrieves data for each predicate in a mapping source.
   */
  public function extractPropertyValues(LdImportItem $item, $predicates) {
    $values = array();

    $predicate = array_shift($predicates);

    // Handle special mapping keywords "URI" and "Label".
    if (strtolower($predicate['uri']) == 'uri') {
      $values[] = $item->uri;
      return $values;
    }
    elseif (strtolower($predicate['uri']) == 'label') {
      $values[] = $item->label;
      return $values;
    }

    $arc = ($this->config['use_caching']) ? $item->arc : NULL;

    // Only proceed if there is data for this predicate.
    if ($property_values = $item->values($predicate['uri'])) {

      $next_predicate = current($predicates);

      foreach ($property_values as $property_value) {

        // Recurse into next predicate when there is one.
        if ($next_predicate && $property_value['type'] == 'uri') {
          $next_item = new LdImportItem($property_value['value'], $arc, $item->language);

          if (!$predicate['filters'] || ($predicate['filters'] && $next_item->hasAnyType($predicate['filters']))) {
            $values = array_merge($values, $this->extractPropertyValues($next_item, $predicates));
          }
        }
        // Add values.
        elseif (!$next_predicate) {
          $values[] = $property_value['value'];
        }
      }
    }

    return $values;
  }

  /**
   * Turns a mapping string into an array of predicates, keyed by predicate URI,
   * with an array of rdf:type URIs as values.
   */
  public function getPredicates($string) {
    $predicates = array();

    $parts = explode(' ', $string);

    foreach ($parts as $k => $part) {

      // Even keys should always be predicate URIs.
      if ($k % 2 === 0) {

        $next_part = $k + 1;

        $types = '';
        if (isset($parts[$next_part])) {
          $types = $parts[$next_part];
          $types = strtr($types, array('[' => '', ']' => ''));
        }

        $predicate = array();
        $predicate['uri'] = $part;
        $predicate['filters'] = !empty($types) ? explode('|', $types) : array();
        $predicates[] = $predicate;
      }
    }

    return $predicates;
  }

  /**
   * Overrides FeedsParser::getMappingSources().
   *
   * This makes the Feeds mapping UI use text inputs instead of dropdowns.
   */
  public function getMappingSources() {
    return FALSE;
  }


  /**
   * Default settings.
   */
  public function configDefaults() {
    return array(
      'use_caching' => 0,
      'batch_limit' => 10,
      'pre_fetch' => TRUE,
      'retries' => 3,
      'timeout' => 30,
    );
  }

  /**
   * Parser settings form.
   */
  public function configForm(&$form_state) {
    $form = array();
    $form['use_caching'] = array(
      '#type' => 'checkbox',
      '#title' => t('Cache requested data'),
      '#description' => t('Cache requested linked data between feed items. This can be problematic if the server returns data for resources other than the requested URI.'),
      '#default_value' => $this->config['use_caching'],
    );
    $form['batch_limit'] = array(
      '#type' => 'textfield',
      '#title' => t('Batch limit'),
      '#description' => t('How many items to retrieve in each Batch API pass. Keeping this low helps to avoid timeouts.'),
      '#default_value' => $this->config['batch_limit'],
      '#size' => 3,
    );
    $form['pre_fetch'] = array(
      '#type' => 'checkbox',
      '#title' => t('Pre-fetch items'),
      '#description' => t('Fetch items during the parsing stage. Items will only be processed when property data is available. The only reason to turn this off is to save HTTP requests when using "Do not update existing nodes" for the processor.'),
      '#default_value' => $this->config['pre_fetch'],
    );
    $form['retries'] = array(
      '#type' => 'textfield',
      '#title' => t('Retry limit'),
      '#description' => t('How many items to retry failed requests for URIs.'),
      '#default_value' => $this->config['retries'],
      '#size' => 3,
    );
    $form['timeout'] = array(
      '#type' => 'textfield',
      '#title' => t('Timeout'),
      '#description' => t('Amount of time, in seconds, to wait for linked data requests.'),
      '#default_value' => $this->config['timeout'],
      '#size' => 3,
    );
    return $form;
  }

  /**
   * Parser settings form validation.
   */
  public function configFormValidate(&$values) {
    $batch_limit = $values['batch_limit'];
    if (!is_numeric($batch_limit) || intval($batch_limit) != $batch_limit || $batch_limit <= 0) {
      form_set_error('batch_limit', t('Only positive integers are allowed.'));
    }
  }
}

/**
 * Class definition for LdimportParserResult.
 */
class LdimportParserResult extends FeedsParserResult {

  public function __construct($uri_list = array(), FeedsParser $parser, FeedsSource $source) {
    $this->items = $uri_list;
    $this->parser = $parser;
    $this->source = $source;
  }

  public function shiftItem() {
    $this->current_item = $this->parser->getItem($this->items, $this->source);
    return $this->current_item;
  }
}
