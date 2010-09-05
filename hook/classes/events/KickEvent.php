<?php

class KickEvent {

    public static $databaseTable = "kicks";
    private $id, $timestamp, $server, $player, $target;

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

    public function getTarget() {
        return $this->target;
    }

    public function setTarget($target) {
        $this->target = $target;
    }

    public function insert() {
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, server_id, player_id, target_id) VALUES(now(), :server_id, :player_id, :target_id)");
        $stmt->execute(array(
            'server_id' => $this->server->getId(),
            'player_id' => $this->player->getId(),
            'target_id' => $this->target->getId()
        ));
    }

}

?>