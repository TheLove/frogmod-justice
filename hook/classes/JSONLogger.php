<?php

class JSONLogger {

    public static $databaseTable = "json";
    private $id, $timestamp, $json;

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

    public function getJson() {
        return $this->json;
    }

    public function setJson($json) {
        $this->json = preg_replace("|[\r\n\t]|", "", $json);
    }

    public function insert() {
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare("INSERT INTO " . self::$databaseTable . " (timestamp, json) VALUES(now(), :json)");
        $stmt->execute(array(
            "json" => $this->json
        ));
    }
}


?>
