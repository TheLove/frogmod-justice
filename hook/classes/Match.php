<?php

class Match extends BasicDatabaseModel {

    public static $databaseTable = "matches";
    private $server, $map, $gameMode;

    function __construct($id, $timestamp, $server, $map, $gameMode) {
        $this->id = $id;
        $this->timestamp = $timestamp;
        $this->server = $server;
        $this->map = $map;
        $this->gameMode = $gameMode;
    }

    /**
     * @return Server
     */
    public function getServer() {
        return $this->server;
    }

    public function setServer($server) {
        $this->server = $server;
    }

    public function getMap() {
        return $this->map;
    }

    public function setMap($map) {
        $this->map = $map;
    }

    public function getGameMode() {
        return $this->gameMode;
    }

    public function setGameMode($gameMode) {
        $this->gameMode = $gameMode;
    }

    public function insert() {
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, server_id, map, gamemode) VALUES(:timestamp, :server_id, :map, :gamemode)");
        $stmt->execute(array(
            'timestamp' => $this->getTimestampString(),
            'server_id' => $this->getServer()->getId(),
            'map' => $this->getMap(),
            'gamemode' => $this->getGameMode()
        ));
        $this->setId($pdo->lastInsertId());
    }

    public static function create($server, $map, $gameMode, $timestamp = null) {
        $match = new Match(0, $timestamp ? $timestamp : time(), $server, $map, $gameMode);
        $match->insert();
        if ($match->getId()) {
            return $match;
        } else {
            return null;
        }
    }

}

?>