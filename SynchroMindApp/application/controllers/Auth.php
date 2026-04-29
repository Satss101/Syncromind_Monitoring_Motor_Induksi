<?php
// application/controllers/Auth.php
defined('BASEPATH') OR exit('No direct script access allowed');

class Auth extends Auth_Controller
{
    public function __construct()
    {
        parent::__construct();
    }

    // ------------------------------------------------------------------
    // GET /login
    // ------------------------------------------------------------------
    public function index()
    {
        $this->login();
    }

    public function login()
    {
        $data['title'] = 'Login — ShynCroMind';
        $this->render('auth/login', $data);
    }

    // ------------------------------------------------------------------
    // POST /login  (submit form)
    // ------------------------------------------------------------------
    public function do_login()
    {
        // Basic CSRF sudah dihandle CI jika csrf_protection = TRUE
        $this->form_validation->set_rules('login',    'Username / Email', 'required|trim');
        $this->form_validation->set_rules('password', 'Password',         'required');

        if (!$this->form_validation->run()) {
            $this->session->set_flashdata('error', validation_errors('<p class="mb-0">', '</p>'));
            redirect('login');
        }

        $login    = $this->input->post('login',    true);
        $password = $this->input->post('password', true);
        $remember = (bool) $this->input->post('remember_me');

        $result = $this->User_model->attempt_login($login, $password);

        switch ($result['status']) {
            case 'success':
                $user = $result['user'];
                $this->session->set_userdata([
                    'user_id'   => $user->id,
                    'user_name' => $user->full_name,
                    'user_role' => $user->role_name,
                ]);

                if ($remember) {
                    $token = $this->User_model->create_remember_token($user->id);
                    set_cookie('remember_token', $token, 86400 * 30);
                }

                $this->User_model->log_activity($user->id, 'login', "Login dari IP: " . $this->input->ip_address());

                $redirect = $this->session->flashdata('redirect_after_login') ?: 'dashboard';
                redirect($redirect);
                break;

            case 'locked':
                $this->session->set_flashdata('error',
                    "Akun dikunci karena terlalu banyak percobaan gagal. Coba lagi dalam {$result['remaining']} menit.");
                redirect('login');
                break;

            case 'wrong_password':
                $sisa = User_model::MAX_LOGIN_ATTEMPTS - $result['attempts'];
                $this->session->set_flashdata('error',
                    "Password salah. Sisa percobaan: {$sisa}");
                redirect('login');
                break;

            default:
                $this->session->set_flashdata('error', 'Username / email tidak ditemukan.');
                redirect('login');
        }
    }

    // ------------------------------------------------------------------
    // GET+POST /logout
    // ------------------------------------------------------------------
    public function logout()
    {
        $user_id = $this->session->userdata('user_id');
        if ($user_id) {
            $this->load->model('User_model');
            $this->User_model->log_activity($user_id, 'logout', 'User logout');

            $token = get_cookie('remember_token');
            if ($token) {
                $this->User_model->delete_remember_token($token);
                delete_cookie('remember_token');
            }
        }

        $this->session->sess_destroy();
        redirect('login');
    }

    // ------------------------------------------------------------------
    // GET /forgot-password
    // ------------------------------------------------------------------
    public function forgot_password()
    {
        $data['title'] = 'Lupa Password — ShynCroMind';
        $this->render('auth/forgot_password', $data);
    }

    // ------------------------------------------------------------------
    // POST /forgot-password (kirim email reset)
    // ------------------------------------------------------------------
    public function send_reset()
    {
        $this->form_validation->set_rules('email', 'Email', 'required|trim|valid_email');

        if (!$this->form_validation->run()) {
            $this->session->set_flashdata('error', validation_errors());
            redirect('forgot-password');
        }

        $email  = $this->input->post('email', true);
        $result = $this->User_model->set_reset_token($email);

        // Selalu tampilkan pesan sukses (keamanan — jangan bocorkan email terdaftar)
        $this->session->set_flashdata('success',
            'Jika email terdaftar, link reset password telah dikirim. Silakan cek inbox Anda.');

        if ($result) {
            $this->load->library('email');
            $reset_url = base_url("reset-password/{$result['token']}");

            $this->email->from('no-reply@shyncromind.local', 'ShynCroMind System');
            $this->email->to($result['user']->email);
            $this->email->subject('Reset Password — ShynCroMind');
            $this->email->message(
                "Halo {$result['user']->full_name},\n\n" .
                "Klik link berikut untuk reset password Anda (berlaku 1 jam):\n{$reset_url}\n\n" .
                "Abaikan email ini jika Anda tidak melakukan permintaan reset."
            );
            $this->email->send();
        }

        redirect('forgot-password');
    }

    // ------------------------------------------------------------------
    // GET /reset-password/:token
    // ------------------------------------------------------------------
    public function reset_password($token)
    {
        $user = $this->User_model->get_user_by_reset_token($token);
        if (!$user) {
            $this->session->set_flashdata('error', 'Link reset tidak valid atau sudah kadaluarsa.');
            redirect('forgot-password');
        }

        $data = ['title' => 'Reset Password — ShynCroMind', 'token' => $token];
        $this->render('auth/reset_password', $data);
    }

    // ------------------------------------------------------------------
    // POST /reset-password (simpan password baru)
    // ------------------------------------------------------------------
    public function do_reset()
    {
        $this->form_validation->set_rules('token',            'Token',            'required');
        $this->form_validation->set_rules('password',         'Password Baru',    'required|min_length[8]');
        $this->form_validation->set_rules('password_confirm', 'Konfirmasi Password', 'required|matches[password]');

        if (!$this->form_validation->run()) {
            $token = $this->input->post('token', true);
            $this->session->set_flashdata('error', validation_errors());
            redirect("reset-password/{$token}");
        }

        $token = $this->input->post('token', true);
        $user  = $this->User_model->get_user_by_reset_token($token);

        if (!$user) {
            $this->session->set_flashdata('error', 'Link reset tidak valid atau sudah kadaluarsa.');
            redirect('forgot-password');
        }

        $this->User_model->reset_password($user->id, $this->input->post('password', true));
        $this->User_model->log_activity($user->id, 'reset_password', 'Password direset via link email');

        $this->session->set_flashdata('success', 'Password berhasil diubah. Silakan login.');
        redirect('login');
    }
}