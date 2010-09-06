<?php

class AutoLoader {

    public static $instance = NULL;
    public static $directories = array(
        'classes',
        'classes/events'
    );
    public static $extensions = array(
        'php'
    );

    public static function createInstance() {
        if (self::$instance == NULL)
            self::$instance = new AutoLoader();
        return self::$instance;
    }

    public function __construct() {
        spl_autoload_register(array($this, 'loadClass'));
    }

    public function loadClass($className) {
        foreach (self::$directories as $directory) {
            $className = preg_replace('|[^a-zA-Z0-9\.]|', '_', $className);
            foreach (self::$extensions as $extension) {
                $filePath = $directory . '/' . $className . '.' . $extension;
                if (file_exists($filePath)) {
                    include($filePath);
                }
            }
        }
    }

}

?>
