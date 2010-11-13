<?php
error_reporting(E_ALL);
require("config.php");
require("classes/AutoLoader.php");
AutoLoader::createInstance();

/**
 * Command line testing
 */
if (isset($argv[1]) && $argv[1]) {
    $jsonString = $argv[1];
    $_SERVER['REMOTE_ADDR'] = "127.0.0.1";
    Config::$debug = true;

    /**
     * HTTP Input
     */
} elseif($_SERVER['REQUEST_METHOD'] == 'POST') {
    $jsonString = trim(file_get_contents('php://input'));
} else { ?>
<h1>Frogmod hook</h1>
<?
}

if (!$jsonString)
    exit();

/**
 * JSON Logger
 */
$jsonLogger = new JSONLogger();
$jsonLogger->setJson($jsonString);
$jsonLogger->setIpAddress($_SERVER['REMOTE_ADDR']);
$jsonLogger->insert();


$jsonParser = new JSONParser();
try {
    $jsonParser->parse($jsonString);
} catch (Exception $e) {
    die("Exception:" . $e->getMessage());
}
