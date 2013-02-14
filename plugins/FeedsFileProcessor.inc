<?php

/**
 * @file
 * Class definition of FeedsNodeProcessor.
 */

/**
 * Creates nodes from feed items.
 */
class FeedsFileProcessor extends FeedsProcessor {
  /**
   * Define entity type.
   */
  public function entityType() {
    return 'file';
  }

  /**
   * Implements parent::entityInfo().
   */
  protected function entityInfo() {
    $info = parent::entityInfo();
    $info['label plural'] = t('Files');
    return $info;
  }

  /**
   * Creates a new file in memory and returns it.
   */
  protected function newEntity(FeedsSource $source) {
    $file = new stdClass();
    //$file->type = $this->config['file_type'] ? $this->config['file_type'] : FILE_TYPE_NONE;
    $file->type = FILE_TYPE_NONE;
    $file->timestamp = REQUEST_TIME;
    $file->uid = $this->config['author'];
    $file->status = FILE_STATUS_PERMANENT;
    return $file;
  }

  /**
   * Loads an existing file.
   */
  protected function entityLoad(FeedsSource $source, $fid) {
    $file = file_load($fid);

    return $file;
  }

  /**
   * Save a file.
   */
  public function entitySave($entity) {
    file_save($entity);
  }

  /**
   * Delete a series of files.
   */
  protected function entityDeleteMultiple($fids) {
    $files = file_load_multiple($fids);
    foreach ($files as $file) {
      file_delete($file);
    }
  }

  /**
   * Override parent::configDefaults().
   */
  public function configDefaults() {
    $entity_info = entity_get_info('file');
    $types = $entity_info['bundles'];

    $type = isset($types['image']) ? 'image' : key($types);
    return array(
      'file_type' => $type,
      'author' => 0,
    ) + parent::configDefaults();
  }

  /**
   * Override parent::configForm().
   */
  public function configForm(&$form_state) {
    $entity_info = entity_get_info('file');
    $types = $entity_info['bundles'];

    $options = array();
    foreach ($types as $type => $bundle_info) {
      $options[$type] = $bundle_info['label'];
    }

    $form = parent::configForm($form_state);

    $form['file_type'] = array(
      '#type' => 'select',
      '#title' => t('File type'),
      '#description' => t('Select the file type for the files to be created.', array('!feed_id' => $this->id)),
      '#options' => $options,
      '#default_value' => $this->config['file_type'],
    );
    $author = user_load($this->config['author']);
    $form['author'] = array(
      '#type' => 'textfield',
      '#title' => t('Author'),
      '#description' => t('Select the author of the files to be created - leave empty to assign "anonymous".'),
      '#autocomplete_path' => 'user/autocomplete',
      '#default_value' => empty($author->name) ?  'anonymous' : check_plain($author->name),
    );

    $form['update_existing']['#options'] = array(
      FEEDS_SKIP_EXISTING => 'Do not update existing files',
      FEEDS_REPLACE_EXISTING => 'Replace existing files',
      FEEDS_UPDATE_EXISTING => 'Update existing files (slower than replacing them)',
    );

    return $form;
  }

  /**
   * Override parent::configFormValidate().
   */
  public function configFormValidate(&$values) {
    if ($author = user_load_by_name($values['author'])) {
      $values['author'] = $author->uid;
    }
    else {
      $values['author'] = 0;
    }
  }

  /**
   * Override setTargetElement to operate on a target item that is a file.
   */
  public function setTargetElement(FeedsSource $source, $target_file, $target_element, $value) {
    switch ($target_element) {
      case 'uri':
        $uri = NULL;
        if (function_exists('media_parse_to_uri')) {
          $uri = media_parse_to_uri($value);
        }

        if (isset($uri)) {
          // Get full file object. This will had a FID if the URI already exists.
          $file = file_uri_to_object($uri);

          $target_file->filename = $file->filename;
          $target_file->uri = $file->uri;
          $target_file->filemime = $file->filemime;
          $target_file->filesize = $file->filesize;
          $target_file->status = $file->status;
        }
        else {
          $target_file->uri = $value;
        }
        break;
      case 'user_name':
        if ($user = user_load_by_name($value)) {
          $target_file->uid = $user->uid;
        }
        break;
      case 'user_mail':
        if ($user = user_load_by_mail($value)) {
          $target_file->uid = $user->uid;
        }
        break;
      default:
        parent::setTargetElement($source, $target_file, $target_element, $value);
        break;
    }
  }

  /**
   * Return available mapping targets.
   */
  public function getMappingTargets() {
    $targets = parent::getMappingTargets();
    $targets['fid'] = array(
      'name' => t('File ID'),
      'description' => t('The fid of the file. NOTE: use this feature with care, file ids are usually assigned by Drupal.'),
      'optional_unique' => TRUE,
    );
    $targets['filename'] = array(
      'name' => t('Filename'),
      'description' => t('The filename of the file.'),
      'optional_unique' => TRUE,
    );
    $targets['uri'] = array(
      'name' => t('File URI'),
      'description' => t('The URI of the file. NOTE: use this feature with care, file URIs are usually assigned by Drupal. If the media module is installed, it will attempt to parse the passed URI for remote files.'),
      'optional_unique' => TRUE,
    );
    $targets['type'] = array(
      'name' => t('File Type'),
      'description' => t('The type of the file. NOTE: use this feature with care, file types are usually assigned by Drupal.'),
    );
    $targets['uid'] = array(
      'name' => t('User ID'),
      'description' => t('The Drupal user ID of the file author.'),
    );
    $targets['user_name'] = array(
      'name' => t('Username'),
      'description' => t('The Drupal username of the file author.'),
    );
    $targets['user_mail'] = array(
      'name' => t('User email'),
      'description' => t('The email address of the file author.'),
    );

    // Let other modules expose mapping targets.
    self::loadMappers();
    $entity_type = $this->entityType();
    $bundle = $this->config['file_type'] ? $this->config['file_type'] : FILE_TYPE_NONE;
    drupal_alter('feeds_processor_targets', $targets, $entity_type, $bundle);

    return $targets;
  }

  /**
   * Get nid of an existing feed item node if available.
   */
  protected function existingEntityId(FeedsSource $source, FeedsParserResult $result) {
    if ($fid = parent::existingEntityId($source, $result)) {
      return $fid;
    }

    // Iterate through all unique targets and test whether they do already
    // exist in the database.
    foreach ($this->uniqueTargets($source, $result) as $target => $value) {
      switch ($target) {
        case 'fid':
          $fid = db_query("SELECT fid FROM {file_managed} WHERE fid = :fid", array(':fid' => $value))->fetchField();
          break;
        case 'filename':
          $fid = db_query("SELECT fid FROM {file_managed} WHERE filename = :filename AND type = :type", array(':filename' => $value, ':type' => $this->config['file_type']))->fetchField();
          break;
        case 'uri':
          if (function_exists('media_parse_to_uri')) {
            $uri = media_parse_to_uri($value);
            if (isset($uri)) {
              $file = file_uri_to_object($uri);

              if (isset($file->fid)) {
                $fid = $file->fid;
              }
            }
          }

          if (!$fid) {
            $fid = db_query("SELECT fid FROM {file_managed} WHERE uri = :uri AND type = :type", array(':uri' => $value, ':type' => $this->config['file_type']))->fetchField();
          }
          break;
      }
      if ($fid) {
        // Return with the first fid found.
        return $fid;
      }
    }
    return 0;
  }
}
