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
<h1>Frogmod database</h1>
<?
try {
$pdo = Database::getPDO();
$lastPlayers = $pdo->query("SELECT * FROM disconnects, players WHERE players.id = disconnects.player_id ORDER BY disconnects.timestamp DESC LIMIT 20");
} catch(PDOException $e) {
	echo $e;
}
?>
<h2>Disconnects</h2>
<table border="1">
<tr><th>Time</th><th>Name</th><th>IP</th><th>Duration</th></tr>
<? foreach($lastPlayers as $p) { ?>
	<tr><td><?=date('d.m.Y H:i:s', strtotime($p['timestamp']))?></td><td><?=htmlspecialchars($p['name'])?></td><td><?=$p['ip_address']?></td><td><? printf("%02d:%02d", floor($p['connection_time']/3600000), floor($p['connection_time']/60000)) ?></td></tr>
<? } ?>
</table>

<?
}

if (!isset($jsonString) || !$jsonString)
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
    die("Exception: " . $e->getMessage());
}
