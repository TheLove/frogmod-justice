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
if(isset($_GET['search'] )&& strlen($_GET['search']) > 2) {
	$st = $pdo->prepare("SELECT * FROM players WHERE name LIKE :searchLike OR ip_address = :search LIMIT 100");
	$st->execute(array('searchLike' => '%'.str_replace(array('%', '_'), array('\\%', '\\_'), trim($_GET['search'])).'%', 'search' => $_GET['search']));
	$searchResults = $st->fetchAll(PDO::FETCH_ASSOC);
}
} catch(PDOException $e) {
	echo $e;
}
?>
<a href="?">First page</a><br>
<form action="" metho="GET">
<input type="text" name="search" value="<?=isset($_GET['search'])?htmlspecialchars($_GET['search']):''?>">
<input type="submit" value="Search by name or IP"></input>
<?
if(isset($searchResults)) {
	if($searchResults) { ?>
<h2>Search results for &quot;<?=htmlspecialchars($_GET['search'])?>&quot;</h2>
<table border="1">
<tr><th>Name</th><th>IP</th><? if(function_exists('geoip_country_name_by_name')) { ?><th>Country</th><? } ?></tr>
<? foreach($searchResults as $r) { ?>
<tr>
	<td><?=htmlspecialchars($r['name'])?></td>
	<td><?=$r['ip_address']?></td>
	<? if(function_exists('geoip_country_name_by_name')) { ?>
	<td><?= @geoip_country_name_by_name($r['ip_address']) ?></td>
	<? } ?>
</tr>
<? } ?>
</table>
<? } else echo '<p>No results were found.</p>'; ?>
<? } else { ?>
</form>
<h2>Disconnects</h2>
<table border="1">
<tr><th>Time</th><th>Name</th><th>IP</th><? if(function_exists('geoip_country_name_by_name')) { ?><th>Country</th><? } ?><th>Duration</th></tr>
<? foreach($lastPlayers as $p) { ?>
	<tr>
		<td><?=date('d.m.Y H:i:s', strtotime($p['timestamp']))?></td>
		<td><?=htmlspecialchars($p['name'])?></td>
		<td><?=$p['ip_address']?></td>
		<? if(function_exists('geoip_country_name_by_name')) { ?><td><?= @geoip_country_name_by_name($p['ip_address']); ?></td><? } ?>
		<td><? printf("%02d:%02d", floor($p['connection_time']/3600000), floor($p['connection_time']/60000)) ?></td>
	</tr>
<? } ?>
</table>
<? } ?>
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
