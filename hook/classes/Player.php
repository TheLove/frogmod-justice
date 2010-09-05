<?php

class Player {

    public static $databaseTable = "players";
    private $id, $name, $ipAddress, $skill;

    function __construct($id, $name, $ipAddress, $skill) {
        $this->id = $id;
        $this->name = $name;
        $this->ipAddress = $ipAddress;
        $this->skill = $skill;
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

    public function getSkill() {
        return $this->skill;
    }

    public function setSkill($skill) {
        $this->skill = $skill;
    }

    public function __toString() {
        return "player[id=\"{$this->id}\", name=\"{$this->name}\", ipAddress=\"{$this->ipAddress}\", skill={$this->skill}]";
    }

}

?>