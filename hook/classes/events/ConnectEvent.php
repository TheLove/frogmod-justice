<?php

class ConnectEvent extends BasicDatabaseModel {

    public static $databaseTable = "connects";
    private $server, $player;
    
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
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, server_id, player_id) VALUES(:timestamp, :server_id, :player_id)");
        $stmt->execute(array(
            'timestamp' => $this->getTimestampString(),
            'server_id' => $this->server->getId(),
            'player_id' => $this->player->getId()
        ));
    }

}

?>