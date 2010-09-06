<?php

class MatchPlayer extends BasicDatabaseModel {

    public static $databaseTable = "matchplayers";
    private $match, $player, $frags, $deaths, $flags, $teamkills, $shotDamage, $damage, $effectiveness;

    public function setParametersFromJSON($json)
    {
        if(isset($json->frags, $json->deaths, $json->flags, $json->teamkills, $json->shotdamage, $json->damage, $json->effectiveness))
        {
            $this->setFrags($json->frags);
            $this->setDeaths($json->deaths);
            $this->setFlags($json->flags);
            $this->setTeamkills($json->teamkills);
            $this->setShotDamage($json->shotdamage);
            $this->setDamage($json->damage);
            $this->setEffectiveness($json->effectiveness);
        }
    }

    public function getMatch() {
        return $this->match;
    }

    public function setMatch($match) {
        $this->match = $match;
    }

    public function getPlayer() {
        return $this->player;
    }

    public function setPlayer($player) {
        $this->player = $player;
    }

    public function getFrags() {
        return $this->frags;
    }

    public function setFrags($frags) {
        $this->frags = $frags;
    }

    public function getDeaths() {
        return $this->deaths;
    }

    public function setDeaths($deaths) {
        $this->deaths = $deaths;
    }

    public function getFlags() {
        return $this->flags;
    }

    public function setFlags($flags) {
        $this->flags = $flags;
    }

    public function getTeamkills() {
        return $this->teamkills;
    }

    public function setTeamkills($teamkills) {
        $this->teamkills = $teamkills;
    }

    public function getShotDamage() {
        return $this->shotDamage;
    }

    public function setShotDamage($shotDamage) {
        $this->shotDamage = $shotDamage;
    }

    public function getDamage() {
        return $this->damage;
    }

    public function setDamage($damage) {
        $this->damage = $damage;
    }

    public function getEffectiveness() {
        return $this->effectiveness;
    }

    public function setEffectiveness($effectiveness) {
        $this->effectiveness = $effectiveness;
    }

    public function insert() {
        $pdo = Database::getPDO();
        $stmt = $pdo->prepare(
                        "INSERT INTO " . self::$databaseTable . " (" .
                        "timestamp, match_id, player_id, frags, deaths, flags, teamkills, shotdamage, damage, effectiveness) " .
                        "VALUES(:timestamp, :match_id, :player_id, :frags, :deaths, :flags, :teamkills, :shotdamage, :damage, :effectiveness)"
        );
        $stmt->execute(array(
            'timestamp' => $this->getTimestampString(),
            'match_id' => $this->match->getId(),
            'player_id' => $this->player->getId(),
            'frags' => $this->frags,
            'deaths' => $this->deaths,
            'flags' => $this->flags,
            'teamkills' => $this->teamkills,
            'shotdamage' => $this->shotDamage,
            'damage' => $this->damage,
            'effectiveness' => $this->effectiveness
        ));
        $this->setId($pdo->lastInsertId());
    }

}

?>
