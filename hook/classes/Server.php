<?php

class Server {

    public static $databaseTable = "servers";
    private $id, $name, $ipAddress, $port;

    function __construct($id, $name, $ipAddress, $port) {
        $this->id = $id;
        $this->name = $name;
        $this->ipAddress = $ipAddress;
        $this->port = $port;
    }

    public function getId() {
        return $this->id;
    }

    public function setId($id) {
        $this->id = $id;
    }

    public function getName() {
        return $this->name;
    }

    public function setName($name) {
        $this->name = $name;
    }

    public function getIpAddress() {
        return $this->ipAddress;
    }

    public function setIpAddress($ipAddress) {
        $this->ipAddress = $ipAddress;
    }

    public function getPort() {
        return $this->port;
    }

    public function setPort($port) {
        $this->port = $port;
    }

    public function __toString() {
        return "server[id=\"{$this->id}\", name=\"{$this->name}\", ipAddress=\"{$this->ipAddress}\", port={$this->port}]";
    }
}

?>