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

    $start = ($state->pointer) ? $state->pointer : 0;
    $limit = $source->importer->getLimit();

    $items = array();

    $arc = ($this->config['use_caching']) ? ldimport_get_arc() : NULL;

    // Run through URIs until we hit the limit or finish the list.
    for ($i = $start; $i < count($uri_list) && count($items) < $limit; $i++) {

      $item = new LdImportItem($uri_list[$i], $arc, $GLOBALS['language']->language);

      // Only add this item for processing if it returns data.
      if ($item->hasData()) {
        $items[] = $item;
        $state->created++;
      }
      else {
        $errors = $item->arc->getErrors();
        if ($errors) {
          watchdog('ldimport', 'Importer %id failed importing %uri. The following errors were encountered: @errors', array('%id' => $source->importer->id, '%uri' => $item->uri, '@errors' => implode(' | ', $errors)));
        }
        else {
          watchdog('ldimport', 'Importer %id failed importing %uri. No data was available.', array('%id' => $source->importer->id, '%uri' => $item->uri));
        }
        $state->failed++;
      }

      // Update progress and pointer.
      $state->progress($state->total, $state->created + $state->failed);
      $state->pointer = $i + 1;
    }

    return new FeedsParserResult($items);
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

    // Only proceed if there is data for this predicate.
    if ($property_values = $item->values($predicate)) {

      $next_predicate = next($predicates);

      foreach ($property_values as $property_value) {
        if ($next_predicate && $property_value['type'] == 'uri') {
          $next_item = new LdImportItem($property_value['value'], $item->arc, $item->language);
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
    return $form;
  }
}
