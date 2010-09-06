<?php

class DisconnectEvent extends BasicDatabaseModel {

    public static $databaseTable = "disconnects";
    private $server, $player, $connectionTime;

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

    public function getConnectionTime() {
        return $this->connectionTime;
    }

    public function setConnectionTime($connectionTime) {
        $this->connectionTime = $connectionTime;
    }

    public function insert() {
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, server_id, player_id, connection_time) VALUES(:timestamp, :server_id, :player_id, :connection_time)");
        $stmt->execute(array(
            'timestamp' => $this->getTimestampString(),
            'server_id' => $this->server->getId(),
            'player_id' => $this->player->getId(),
            'connection_time' => $this->connectionTime
        ));
    }

}

?>