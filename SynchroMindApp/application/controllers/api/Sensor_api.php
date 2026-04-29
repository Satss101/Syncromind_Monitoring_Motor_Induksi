<?php
defined('BASEPATH') OR exit('No direct script access allowed');

class Sensor_api extends CI_Controller
{
    public function __construct()
    {
        parent::__construct();
        $this->load->model('Sensor_model');
    }

    // ============================================================
    // POST /api/sensor
    // ============================================================
    public function index()
    {
        if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
            return $this->_json([
                'status' => false,
                'message' => 'Method tidak diizinkan'
            ], 405);
        }

        $input = json_decode($this->input->raw_input_stream, true);

        if (!$input) {
            return $this->_json([
                'status' => false,
                'message' => 'Invalid JSON'
            ], 400);
        }

        // Validasi field wajib
        $required = [
            'suhu_mesin', 'accx', 'accy', 'accz',
            'suhu_lingkungan', 'kelembapan',
            'arusp1', 'arusp2', 'arusp3',
            'teganganp1', 'teganganp2', 'teganganp3'
        ];

        foreach ($required as $field) {
            if (!isset($input[$field])) {
                return $this->_json([
                    'status' => false,
                    'message' => "Field {$field} wajib diisi"
                ], 422);
            }
        }

        $data = [
            'suhu_mesin'       => $input['suhu_mesin'],
            'accx'             => $input['accx'],
            'accy'             => $input['accy'],
            'accz'             => $input['accz'],
            'suhu_lingkungan'  => $input['suhu_lingkungan'],
            'kelembapan'       => $input['kelembapan'],
            'arusp1'           => $input['arusp1'],
            'arusp2'           => $input['arusp2'],
            'arusp3'           => $input['arusp3'],
            'teganganp1'       => $input['teganganp1'],
            'teganganp2'       => $input['teganganp2'],
            'teganganp3'       => $input['teganganp3'],
        ];

        $this->Sensor_model->insert($data);

        return $this->_json([
            'status' => true,
            'message' => 'Data sensor berhasil disimpan'
        ], 201);
    }

    // ============================================================
    // Helper JSON
    // ============================================================
    private function _json($data, $status = 200)
    {
        return $this->output
            ->set_status_header($status)
            ->set_content_type('application/json')
            ->set_output(json_encode($data));
    }
}