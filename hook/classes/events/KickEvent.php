<?php

class KickEvent extends BasicDatabaseModel {

    public static $databaseTable = "kicks";
    private $server, $player, $target;

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
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, server_id, player_id, target_id) VALUES(:timestamp, :server_id, :player_id, :target_id)");
        $stmt->execute(array(
            'timestamp' => $this->getTimestampString(),
            'server_id' => $this->server->getId(),
            'player_id' => $this->player->getId(),
            'target_id' => $this->target->getId()
        ));
    }

}

?>