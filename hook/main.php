<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN"
            "http://www.w3.org/TR/html4/loose.dtd">
<html>
<head>
<link rel="stylesheet" href="css/hook.css" type="text/css">
<link rel="stylesheet" href="css/south-street/jquery-ui-1.8.9.custom.css" type="text/css">
<script type="text/javascript" src="js/jquery-1.5.min.js"></script>
<script type="text/javascript" src="js/jquery-ui-1.8.9.custom.min.js"></script>
<script type="text/javascript">
$(function() {
	$('#tabs').tabs();
});
</script>
<title>Frogmod database</title>
</head>
<body>
<h1>Frogmod database</h1>
<?
try {
$pdo = Database::getPDO();
$lastPlayers = $pdo->query("
	SELECT
		disconnects.connection_time,
		disconnects.timestamp,
		players.name,
		players.ip_address,
		servers.name AS server
	FROM
		disconnects,
		players,
		servers
	WHERE players.id = disconnects.player_id
	  AND servers.id = disconnects.server_id
	ORDER BY disconnects.timestamp DESC
	LIMIT 20");
$kicks = $pdo->query("
	SELECT
		kicks.timestamp,
		servers.name AS server_name,
		players.name AS player_name,
		players.ip_address AS player_ip,
		targets.name AS target_name,
		targets.ip_address AS target_ip
	FROM
		kicks,
		players,
		players targets,
		servers
	WHERE kicks.player_id = players.id
	  AND kicks.server_id = servers.id
	  AND kicks.target_id = targets.id
	ORDER BY kicks.timestamp DESC
	LIMIT 20");
if(isset($_GET['search'] )&& strlen($_GET['search']) > 2) {
	$st = $pdo->prepare("SELECT * FROM players WHERE name LIKE :searchLike OR ip_address = :search LIMIT 100");
	$st->execute(array('searchLike' => '%'.str_replace(array('%', '_'), array('\\%', '\\_'), trim($_GET['search'])).'%', 'search' => $_GET['search']));
	$searchResults = $st->fetchAll(PDO::FETCH_ASSOC);
}
} catch(PDOException $e) {
	echo $e;
}
?>
<div id="tabs">
	<ul>
		<li><a href="#results">Search<?=isset($searchResults)?' results for &quot;'.htmlspecialchars($_GET['search']).'&quot;':''?></a></li>
		<li><a href="#disconnects">Disconnects</a>
		<li><a href="#kicks">Kicks</a></li>
	</ul>
	<div id="results">
<form action="" method="GET">
<input type="text" name="search" value="<?=isset($_GET['search'])?htmlspecialchars($_GET['search']):''?>">
<input type="submit" value="Search by name or IP"></input>
<a href="?">Clear</a><br>
</form>

<? if(isset($searchResults) && $searchResults) { ?>
<table border="1">
<tr><th>Name</th><th>IP</th><? if(function_exists('geoip_country_name_by_name')) { ?><th>Country</th><? } ?></tr>
<? foreach($searchResults as $r) { ?>
<tr>
	<td><a href="?search=<?=urlencode($r['name'])?>"><?=htmlspecialchars($r['name'])?></a></td>
	<td><a href="?search=<?=urlencode($r['ip_address'])?>"><?=$r['ip_address']?></a></td>
	<? if(function_exists('geoip_country_name_by_name')) { ?>
	<td><?= @geoip_country_name_by_name($r['ip_address']) ?></td>
	<? } ?>
</tr>
<? } ?>
</table>
<? } ?>
</div>
<div id="disconnects">
<table border="1">
<tr><th>Time</th><th>Server</th><th>Name</th><th>IP</th><? if(function_exists('geoip_country_name_by_name')) { ?><th>Country</th><? } ?><th>Duration</th></tr>
<? foreach($lastPlayers as $p) { ?>
	<tr>
		<td><?=date('d.m.Y H:i:s', strtotime($p['timestamp']))?></td>
		<td><?=htmlspecialchars($p['server'])?></td>
		<td><a href="?search=<?=urlencode($p['name'])?>"><?=htmlspecialchars($p['name'])?></a></td>
		<td><a href="?search=<?=$p['ip_address']?>"><?=$p['ip_address']?></a></td>
		<? if(function_exists('geoip_country_name_by_name')) { ?><td><?= @geoip_country_name_by_name($p['ip_address']); ?></td><? } ?>
		<td><? printf("%02d:%02d:%02d", floor($p['connection_time']/3600000), floor($p['connection_time']/60000)%60, ($p['connection_time'] / 1000) % 60) ?></td>
	</tr>
<? } ?>
</table>
</div>
<div id="kicks">
<table border="1">
<tr>
	<th>Time</th>
	<th>Server</th>
	<th>Player</th>
	<th>Player IP</th>
	<? if(function_exists('geoip_country_name_by_name')) { ?><th>Country</th><? } ?>
	<th>Target</th>
	<th>Target IP</th>
	<? if(function_exists('geoip_country_name_by_name')) { ?><th>Country</th><? } ?>
</tr>
<? foreach($kicks as $kick) { ?>
<tr>
	<td><?=date('d.m.Y H:i:s', strtotime($kick['timestamp']))?></td>
	<td><?=htmlspecialchars($kick['server_name'])?></td>
	<td><a href="?search=<?=urlencode($kick['player_name'])?>"><?=htmlspecialchars($kick['player_name'])?></a></td>
	<td><a href="?search=<?=urlencode($kick['player_ip'])?>"><?=htmlspecialchars($kick['player_ip'])?></a></td>
	<? if(function_exists('geoip_country_name_by_name')) { ?><td><?= @geoip_country_name_by_name($kick['player_ip']); ?></td><? } ?>
	<td><a href="?search=<?=urlencode($kick['target_name'])?>"><?=htmlspecialchars($kick['target_name'])?></a></td>
	<td><a href="?search=<?=urlencode($kick['target_ip'])?>"><?=htmlspecialchars($kick['target_ip'])?></a></td>
	<? if(function_exists('geoip_country_name_by_name')) { ?><td><?= @geoip_country_name_by_name($kick['target_ip']); ?></td><? } ?>
</tr>
<? } ?>
</table>
</div>
</div><!-- id="tabs" -->
</body>
</html>
