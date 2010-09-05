<?php

class Match {

    public static $databaseTable = "matches";
    private $id, $server, $map, $gameMode;

    function __construct($id, $server, $map, $gameMode) {
        $this->id = $id;
        $this->server = $server;
        $this->map = $map;
        $this->gameMode = $gameMode;
    }


    public function getId() {
        return $this->id;
    }

    public function setId($id) {
        $this->id = $id;
    }

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

}

?>