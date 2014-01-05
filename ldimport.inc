<?php

/**
 * @file
 * Classes used by the Linked Data Import module.
 */

/**
 * Wrapper around an ARC2_Resource object.
 *
 * This class is used for Feeds items returned by LdImportParser.
 */
class LdImportItem {

  protected $arc;
  protected $uri;
  protected $label;
  protected $language;

  public function __construct($uri, ARC2_Resource $arc = NULL, $language = NULL) {
    $this->uri = $uri;
    $this->arc = ($arc) ? $arc : ARC2::getResource();
    $this->language = $language;
    $this->label = ($values = $this->values(LDIMPORT_LABEL_URI)) ? $values[0]['value'] : $this->arc->extractTermLabel($uri);
  }

  public function __get($name) {
    return property_exists($this, $name) ? $this->$name : NULL;
  }

  public function hasData() {
    return ($this->arc->getProps(NULL, $this->uri)) ? TRUE : FALSE;
  }

  public function values($prop_uri) {
    $values = $this->arc->getProps($prop_uri, $this->uri);
    return ($values) ? $this->filterLanguage($values) : array();
  }

  protected function filterLanguage($items) {
    if ($this->language) {
      foreach ($items as $key => $item) {
        if (isset($item['lang']) && (strpos($item['lang'], $this->language) !== 0))  {
          unset($items[$key]);
        }
      }
    }
    return array_values($items);
  }

  /**
   * Omit cached data from $this->arc when serializing. This leads to accurate
   * hashing for FeedsProcessor::hash().
   */
  public function __sleep() {
    $this->props = $this->arc->getProps(NULL, $this->uri);
    return array('uri', 'props');
  }
}
