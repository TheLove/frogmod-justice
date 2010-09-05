<?php

class SuicideEvent {

    public static $databaseTable = "suicides";
    private $id, $timestamp, $server, $player;

    public function getId() {
        return $this->id;
    }

    public function setId($id) {
        $this->id = $id;
    }

    public function getTimestamp() {
        return $this->timestamp;
    }

    public function setTimestamp($timestamp) {
        $this->timestamp = $timestamp;
    }

    public function getServer() {
        return $this->server;
    }

    public function setServer($server) {
        $this->server = $server;
    }

    public function getPlayer() {
        return $this->player;
    }

    public function setPlayer($player) {
        $this->player = $player;
    }

    public function insert() {
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, server_id, player_id) VALUES(now(), :server_id, :player_id)");
        $stmt->execute(array(
            'server_id' => $this->server->getId(),
            'player_id' => $this->player->getId()
        ));
    }

}

?>