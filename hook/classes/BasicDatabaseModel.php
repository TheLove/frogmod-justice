<?php

class BasicDatabaseModel {

    protected $id, $timestamp;

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

    public function getTimestampString() {
        if (!$this->timestamp) {
            $timestamp = time();
        } else {
            $timestamp = $this->timestamp;
        }
        return date("Y-m-d H:i:s", $timestamp);
    }

}

?>
