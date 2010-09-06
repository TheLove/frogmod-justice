<?php

class JSONLogger extends BasicDatabaseModel {

    public static $databaseTable = "json";
    private $ipAddress, $json;

    public function getIpAddress() {
        return $this->ipAddress;
    }

    public function setIpAddress($ipAddress) {
        $this->ipAddress = $ipAddress;
    }

    public function getJson() {
        return $this->json;
    }

    public function setJson($json) {
        $this->json = preg_replace("|[\r\n\t]|", "", $json);
    }

    public function insert($timestamp = null) {
        if ($timestamp == null)
            $timestamp = time();
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, ip_address, json) VALUES(:timestamp, :ip_address, :json)");
        $stmt->execute(array(
            'timestamp' => $this->getTimestampString(),
            'ip_address' => $this->ipAddress,
            'json' => $this->json
        ));
    }

}

?>
