<?php
// application/core/MY_Controller.php
defined('BASEPATH') OR exit('No direct script access allowed');

/**
 * MY_Controller
 * Base controller — semua controller lain extend dari sini.
 * Menyediakan proteksi autentikasi, role-based access, dan helper view.
 */
class MY_Controller extends CI_Controller
{
    protected $current_user  = null;
    protected $current_role  = null;
    protected $allowed_roles = [];   // override di child controller

    public function __construct()
    {
        parent::__construct();
        $this->load->model('User_model');
        $this->_check_auth();
    }

    // ------------------------------------------------------------------
    // Cek sesi login
    // ------------------------------------------------------------------
    private function _check_auth()
    {
        $user_id = $this->session->userdata('user_id');

        if (!$user_id) {
            // Coba remember-me cookie
            $user_id = $this->_check_remember_me();
        }

        if ($user_id) {
            $this->current_user = $this->User_model->get_by_id($user_id);

            if (!$this->current_user || !$this->current_user->is_active) {
                $this->_force_logout();
                return;
            }

            $this->current_role = $this->current_user->role_name;
            $this->_check_role_access();
        } else {
            $this->_redirect_to_login();
        }
    }

    // ------------------------------------------------------------------
    // Cek remember-me cookie
    // ------------------------------------------------------------------
    private function _check_remember_me()
    {
        $token = get_cookie('remember_token');
        if (!$token) return null;

        $session_row = $this->User_model->get_session_by_token($token);
        if (!$session_row || strtotime($session_row->expires_at) < time()) {
            delete_cookie('remember_token');
            return null;
        }

        // Perbarui sesi CI
        $this->session->set_userdata('user_id', $session_row->user_id);
        return $session_row->user_id;
    }

    // ------------------------------------------------------------------
    // Validasi role
    // ------------------------------------------------------------------
    private function _check_role_access()
    {
        if (empty($this->allowed_roles)) return;   // tidak ada restriksi

        if (!in_array($this->current_role, $this->allowed_roles)) {
            show_error('Akses ditolak. Anda tidak memiliki izin untuk halaman ini.', 403, 'Akses Ditolak');
        }
    }

    // ------------------------------------------------------------------
    // Paksa logout
    // ------------------------------------------------------------------
    protected function _force_logout()
    {
        $this->session->sess_destroy();
        delete_cookie('remember_token');
        redirect('login');
    }

    // ------------------------------------------------------------------
    // Redirect ke login jika belum auth
    // ------------------------------------------------------------------
    protected function _redirect_to_login()
    {
        if (!in_array($this->router->fetch_class(), ['auth', 'errors'])) {
            $this->session->set_flashdata('redirect_after_login', current_url());
            redirect('login');
        }
    }

    // ------------------------------------------------------------------
    // Helper render view dengan layout
    // ------------------------------------------------------------------
    protected function render($view, $data = [], $layout = 'main')
    {
        $data['current_user'] = $this->current_user;
        $data['current_role'] = $this->current_role;
        $data['content_view'] = $view;

        $this->load->view("layouts/{$layout}", $data);
    }

    // ------------------------------------------------------------------
    // JSON response helper (untuk AJAX / API internal)
    // ------------------------------------------------------------------
    protected function json($data, $status = 200)
    {
        $this->output
             ->set_status_header($status)
             ->set_content_type('application/json')
             ->set_output(json_encode($data));
    }
}

// ------------------------------------------------------------------
// Auth_Controller — khusus halaman publik (login, forgot password)
// Tidak memerlukan sesi aktif
// ------------------------------------------------------------------
class Auth_Controller extends CI_Controller
{
    protected $current_user = null;

    public function __construct()
    {
        parent::__construct();
        $this->load->model('User_model');
        $this->load->library(['form_validation', 'session']);
        $this->load->helper(['url', 'form', 'security', 'cookie']);

        // Kalau sudah login, langsung ke dashboard
        if ($this->session->userdata('user_id')) {
            redirect('dashboard');
        }
    }

    protected function render($view, $data = [], $layout = 'auth')
    {
        $data['content_view'] = $view;
        $this->load->view("layouts/{$layout}", $data);
    }

    protected function json($data, $status = 200)
    {
        $this->output
             ->set_status_header($status)
             ->set_content_type('application/json')
             ->set_output(json_encode($data));
    }
}