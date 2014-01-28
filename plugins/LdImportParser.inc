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
    $uri_list = $fetcher_result->getList();
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

    $arc = ($this->config['use_caching']) ? ldimport_get_arc() : NULL;

    $start = ($state->pointer) ? $state->pointer : 0;
    $limit = $this->config['batch_limit'];
    $parsed = $state->parsed;

    // Run through URIs until we hit the limit or finish the list.
    for ($i = $start; $i < count($uri_list) && $parsed < $limit; $i++) {

      $attempts = !empty($this->config['retries']) ? $this->config['retries'] : 1;

      while ($attempts > 0) {
        $uri = $uri_list[$i];
        $item = new LdImportItem($uri, $arc, $GLOBALS['language']->language);

        if (empty($this->config['pre_fetch']) || $item->hasData()) {
          $state->created++;
          $state->parsed++;
          $state->pointer = $i + 1;
          $state->progress($state->total, $state->created + $state->failed);
          return $item;
        }
        elseif (!$attempts) {
          if ($errors = $item->arc->getErrors()) {
            $message = t('Failed importing %uri. The following errors were encountered: @errors', array('%uri' => $uri, '@errors' => implode(' | ', $errors)));
          }
          else {
            $message = t('Failed importing %uri. No data was available.', array('%uri' => $uri));
          }
          $source->log('import', $message, array(), WATCHDOG_ERROR);
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

    $value = '';
    $predicates = explode(' [] ', $element_key);

    if ($element_key == 'URI') {
      $value = $item->uri;
    }
    else if ($element_key == 'Label') {
      $value = $item->label;
    }
    else if ($values = $this->extractPropertyValues($item, $predicates)) {
      $value = (count($values) == 1) ? reset($values) : $values;
    }

    return $value;
  }

  /**
   * Recursively retrieves data for each predicate in a mapping source.
   */
  public function extractPropertyValues(LdImportItem $item, $predicates) {
    $values = array();
    $predicate = current($predicates);
    $arc = ($this->config['use_caching']) ? $item->arc : NULL;

    // Only proceed if there is data for this predicate.
    if ($property_values = $item->values($predicate)) {

      $next_predicate = next($predicates);

      foreach ($property_values as $property_value) {
        if ($next_predicate && $property_value['type'] == 'uri') {
          $next_item = new LdImportItem($property_value['value'], $arc, $item->language);
          $values = array_merge($values, $this->extractPropertyValues($next_item, $predicates));
        }
        else if (!$next_predicate) {
          $values[] = $property_value['value'];
        }
      }
    }

    return $values;
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
