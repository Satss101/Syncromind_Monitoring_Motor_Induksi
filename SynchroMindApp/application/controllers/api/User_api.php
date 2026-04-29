<?php
defined('BASEPATH') OR exit('No direct script access allowed');

class User_api extends CI_Controller
{
    public function __construct()
    {
        parent::__construct();
        $this->load->model('User_model');
    }

    // ============================================================
    // GET /api/users
    // ============================================================
    public function index()
    {
        $method = $_SERVER['REQUEST_METHOD'];

        if ($method === 'GET') {
            $users = $this->User_model->get_all();

            return $this->_json([
                'status' => true,
                'data'   => $users
            ]);
        }

        if ($method === 'POST') {
            return $this->store();
        }

        show_404();
    }

    // ============================================================
    // GET /api/users/{id}
    // ============================================================
    public function show($id)
    {
        $user = $this->User_model->get_by_id($id);

        if (!$user) {
            return $this->_json([
                'status' => false,
                'message' => 'User tidak ditemukan'
            ], 404);
        }

        return $this->_json([
            'status' => true,
            'data'   => $user
        ]);
    }

    // ============================================================
    // POST /api/users
    // ============================================================
    public function store()
    {
        $input = json_decode($this->input->raw_input_stream, true);

        if (!$input) {
            return $this->_json([
                'status' => false,
                'message' => 'Invalid JSON'
            ], 400);
        }

        // validasi sederhana
        if (empty($input['username']) || empty($input['email']) || empty($input['password'])) {
            return $this->_json([
                'status' => false,
                'message' => 'Field wajib tidak lengkap'
            ], 422);
        }

        $data = [
            'role_id'   => $input['role_id'] ?? 3,
            'full_name' => $input['full_name'] ?? '',
            'username'  => $input['username'],
            'email'     => $input['email'],
            'password'  => $input['password'],
        ];

        $id = $this->User_model->create($data);

        return $this->_json([
            'status' => true,
            'message' => 'User berhasil dibuat',
            'user_id' => $id
        ], 201);
    }

    // ============================================================
    // PUT /api/users/{id}
    // ============================================================
    public function update($id)
    {
        $input = json_decode($this->input->raw_input_stream, true);

        $user = $this->User_model->get_by_id($id);
        if (!$user) {
            return $this->_json([
                'status' => false,
                'message' => 'User tidak ditemukan'
            ], 404);
        }

        $data = [];

        if (isset($input['full_name'])) $data['full_name'] = $input['full_name'];
        if (isset($input['email']))     $data['email'] = $input['email'];
        if (isset($input['password']))  $data['password'] = $input['password'];
        if (isset($input['role_id']))   $data['role_id'] = $input['role_id'];

        $this->User_model->update($id, $data);

        return $this->_json([
            'status' => true,
            'message' => 'User berhasil diupdate'
        ]);
    }

    // ============================================================
    // DELETE /api/users/{id}
    // ============================================================
    public function delete($id)
    {
        $user = $this->User_model->get_by_id($id);

        if (!$user) {
            return $this->_json([
                'status' => false,
                'message' => 'User tidak ditemukan'
            ], 404);
        }

        $this->User_model->delete($id);

        return $this->_json([
            'status' => true,
            'message' => 'User berhasil dinonaktifkan'
        ]);
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

    public function detail($id)
    {
        $method = $_SERVER['REQUEST_METHOD'];

        if ($method === 'GET') {
            return $this->show($id);
        }

        if ($method === 'PUT') {
            return $this->update($id);
        }

        if ($method === 'DELETE') {
            return $this->delete($id);
        }

        show_404();
    }
}