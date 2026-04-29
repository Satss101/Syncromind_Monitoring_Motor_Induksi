<?php
defined('BASEPATH') OR exit('No direct script access allowed');

class Sensor_model extends CI_Model
{
    private $table = 'data_sensor';

    public function insert($data)
    {
        return $this->db->insert($this->table, $data);
    }
}