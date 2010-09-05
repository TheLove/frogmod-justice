<?php

require("Config.php");
require("classes/Database.php");
require("classes/events/ConnectEvent.php");
require("classes/events/DisconnectEvent.php");
require("classes/events/KickEvent.php");
require("classes/events/KillEvent.php");
require("classes/events/SuicideEvent.php");
require("classes/JSONLogger.php");
require("classes/Server.php");
require("classes/Player.php");
require("classes/Match.php");
require("classes/MatchPlayer.php");


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
} else {
    $jsonString = trim(file_get_contents('php://input'));
}

if (!$jsonString)
    exit();

/**
 * JSON Logger
 */
$jsonLogger = new JSONLogger();
$jsonLogger->setJson($jsonString);
$jsonLogger->insert();


$event = json_decode($jsonString);

if (!is_object($event)) {
    die("ERROR: could not create a JSON object from '".$jsonString."'");
}

/**
 * Server
 */
if (isset($event->server) && isset($event->server->name) && isset($event->server->port)) {
    $server = Database::getServer($event->server->name, gethostbyname($_SERVER['REMOTE_ADDR']), $event->server->port);
} else {
    die("ERROR: no server found from JSON!");
}

/**
 * Player
 */
if (isset($event->player, $event->player->name, $event->player->ip, $event->player->skill)) {
    $player = Database::getPlayer($event->player->name, $event->player->ip, $event->player->skill);
}


if (false) {

    /**
     * Connect event
     */
} else if ($event->type == "connect" && $server && $player) {
    $connectEvent = new ConnectEvent();
    $connectEvent->setPlayer($player);
    $connectEvent->setServer($server);
    $connectEvent->insert();

    if (Config::$debug)
    echo "type[{$event->type}], $server, $player\n";

    /**
     * Disconnect event
     */
} else if ($event->type == "disconnect" && $server && $player && isset($event->connectionTime)) {
    $disconnectEvent = new DisconnectEvent();
    $disconnectEvent->setPlayer($player);
    $disconnectEvent->setServer($server);
    $disconnectEvent->setConnectionTime($event->connectionTime);
    $disconnectEvent->insert();

    if (Config::$debug)
    echo "type[{$event->type}], $server, $player, connectionTime[{$disconnectEvent->getConnectionTime()}]\n";


    /**
     * Kick event
     */
} else if (
        $event->type == "kick" && $server && $player &&
        isset($event->target, $event->target->name, $event->target->ip, $event->target->skill)
) {
    $target = Database::getPlayer($event->target->name, $event->target->ip, $event->target->skill);
    $kickEvent = new KickEvent();
    $kickEvent->setServer($server);
    $kickEvent->setPlayer($player);
    $kickEvent->setTarget($target);
    $kickEvent->insert();

    if (Config::$debug)
    echo "type[{$event->type}], $server, kicker $player, target $target\n";

    /**
     * Kill event
     */
} else if (
        $event->type == "kill" && $server && $player &&
        isset($event->target, $event->target->name, $event->target->ip, $event->target->skill) &&
        isset($event->gun)
) {
    $target = Database::getPlayer($event->target->name, $event->target->ip, $event->target->skill);
    $killEvent = new KillEvent();
    $killEvent->setServer($server);
    $killEvent->setPlayer($player);
    $killEvent->setTarget($target);
    $killEvent->setGun($event->gun);
    $killEvent->insert();

    if (Config::$debug)
    echo "type[{$event->type}], $server, killer $player, target $target\n";

    /**
     * Suicide event
     */
} else if ($event->type == "suicide" && $server && $player) {
    $suicideEvent = new SuicideEvent();
    $suicideEvent->setServer($server);
    $suicideEvent->setPlayer($player);
    $suicideEvent->insert();

    if (Config::$debug)
    echo "type[{$event->type}], $server, $player\n";

    /**
     * Intermission event
     */
} else if ($event->type == "intermission") {
    if (isset($event->server->map) && isset($event->server->gamemode)) {
        $match = Database::createMatch($server, $event->server->map, $event->server->gamemode);
        if (isset($event->players)) {
            foreach ($event->players as $jsonMatchPlayer) {
                if (isset($jsonMatchPlayer->name, $jsonMatchPlayer->ip, $jsonMatchPlayer->skill)) {
                    $player = Database::getPlayer($jsonMatchPlayer->name, $jsonMatchPlayer->ip, $jsonMatchPlayer->skill);
                    if ($player) {

                        $matchPlayer = new MatchPlayer();
                        $matchPlayer->setMatch($match);
                        $matchPlayer->setPlayer($player);
                        $matchPlayer->setParametersFromJSON($jsonMatchPlayer);
                        $matchPlayer->insert();
                    }
                }
            }
        }
    }
} else {

    if (Config::$debug)
    echo "type[{$event->type}], $server\n";
}
?>