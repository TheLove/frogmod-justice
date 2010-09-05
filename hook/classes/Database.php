<?php

class Database {

    public static $pdo = null;

    /**
     * @return PDO $pdo
     */
    public static function getPDO() {
        if (self::$pdo == null) {
            self::$pdo = new PDO(Config::$databaseDsn, Config::$databaseUsername, Config::$databasePassword);
            return self::$pdo;
        } else {
            return self::$pdo;
        }
    }

    /**
     * @param string $name
     * @param string $ip
     * @return Player $player
     */
    public static function getPlayer($name, $ipAddress, $skill) {
        $pdo = self::getPDO();
        for ($i = 1; $i <= 2; $i++) {
            $stmt = $pdo->prepare("SELECT id, name, ip_address, skill FROM " . Player::$databaseTable . " WHERE name = :name AND ip_address = :ip_address AND skill = :skill");
            $stmt->execute(array(
                "name" => $name,
                "ip_address" => $ipAddress,
                "skill" => $skill
            ));
            $players = $stmt->fetchAll();
            if (!isset($players[0])) {
                $stmt = $pdo->prepare("INSERT INTO " . Player::$databaseTable . " (name, ip_address, skill) VALUES(:name, :ip_address, :skill)");
                $stmt->execute(array(
                    "name" => $name,
                    "ip_address" => $ipAddress,
                    "skill" => $skill
                ));
            } else {
                return new Player($players[0]['id'], $players[0]['name'], $players[0]['ip_address'], $players[0]['skill']);
            }
        }
        return null;
    }

    /**
     * @param string $name
     * @param string $ipAddress
     * @param int $port
     * @return Server $server
     */
    public static function getServer($name, $ipAddress, $port) {
        $pdo = self::getPDO();
        for ($i = 1; $i <= 2; $i++) {
            $stmt = $pdo->prepare("SELECT id, name, ip_address, port FROM " . Server::$databaseTable . " WHERE name = :name AND ip_address = :ip_address AND port = :port");
            $stmt->execute(array(
                "name" => $name,
                "ip_address" => $ipAddress,
                "port" => $port
            ));
            $servers = $stmt->fetchAll();
            if (!isset($servers[0])) {
                $stmt = $pdo->prepare("INSERT INTO " . Server::$databaseTable . " (name, ip_address, port) VALUES(:name, :ip_address, :port)");
                $stmt->execute(array(
                    "name" => $name,
                    "ip_address" => $ipAddress,
                    "port" => $port
                ));
            } else {
                return new Server($servers[0]['id'], $servers[0]['name'], $servers[0]['ip_address'], $servers[0]['port']);
            }
        }
        return null;
    }

    public static function createMatch($server, $map, $gameMode) {
        $pdo = self::getPDO();
        for ($i = 1; $i <= 2; $i++) {
            $stmt = $pdo->prepare("INSERT INTO " . Match::$databaseTable . " (timestamp, server_id, map, gamemode) VALUES(now(), :server_id, :map, :gamemode)");
            $stmt->execute(array(
                "server_id" => $server->getId(),
                "map" => $map,
                "gamemode" => $gameMode
            ));
            $matchId = $pdo->lastInsertId();
            if ($matchId)
                return new Match($matchId, $server, $map, $gameMode);
            else
                return null;
        }
        return null;
    }


}

?>