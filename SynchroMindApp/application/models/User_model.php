<?php
// application/models/User_model.php
defined('BASEPATH') OR exit('No direct script access allowed');

class User_model extends CI_Model
{
    const TABLE_USERS    = 'users';
    const TABLE_ROLES    = 'roles';
    const TABLE_SESSIONS = 'user_sessions';
    const TABLE_LOGS     = 'activity_logs';

    const MAX_LOGIN_ATTEMPTS = 5;
    const LOCK_DURATION_MIN  = 15;
    const REMEMBER_DAYS      = 30;

    // ================================================================
    //  AUTH
    // ================================================================

    /**
     * Attempt login — kembalikan user object atau false.
     */
    public function attempt_login($login, $password)
    {
        $user = $this->db
                     ->select('u.*, r.name AS role_name')
                     ->from(self::TABLE_USERS . ' u')
                     ->join(self::TABLE_ROLES . ' r', 'r.id = u.role_id')
                     ->where('u.is_active', 1)
                     ->group_start()
                         ->where('u.username', $login)
                         ->or_where('u.email', $login)
                     ->group_end()
                     ->get()
                     ->row();

        if (!$user) return ['status' => 'not_found'];

        // Cek apakah akun terkunci
        if ($user->locked_until && strtotime($user->locked_until) > time()) {
            $remaining = ceil((strtotime($user->locked_until) - time()) / 60);
            return ['status' => 'locked', 'remaining' => $remaining];
        }

        if (!password_verify($password, $user->password)) {
            $this->_increment_login_attempts($user);
            return ['status' => 'wrong_password', 'attempts' => $user->login_attempts + 1];
        }

        // Login sukses — reset attempt counter
        $this->db->where('id', $user->id)->update(self::TABLE_USERS, [
            'login_attempts' => 0,
            'locked_until'   => null,
            'last_login'     => date('Y-m-d H:i:s'),
        ]);

        return ['status' => 'success', 'user' => $user];
    }

    private function _increment_login_attempts($user)
    {
        $attempts = $user->login_attempts + 1;
        $data     = ['login_attempts' => $attempts];

        if ($attempts >= self::MAX_LOGIN_ATTEMPTS) {
            $data['locked_until'] = date('Y-m-d H:i:s', time() + self::LOCK_DURATION_MIN * 60);
        }

        $this->db->where('id', $user->id)->update(self::TABLE_USERS, $data);
    }

    /**
     * Buat token remember-me dan simpan ke DB.
     */
    public function create_remember_token($user_id)
    {
        $token = bin2hex(random_bytes(32));

        $this->db->insert(self::TABLE_SESSIONS, [
            'user_id'    => $user_id,
            'token'      => $token,
            'ip_address' => $this->input->ip_address(),
            'user_agent' => substr($this->input->user_agent(), 0, 255),
            'expires_at' => date('Y-m-d H:i:s', time() + self::REMEMBER_DAYS * 86400),
        ]);

        return $token;
    }

    public function get_session_by_token($token)
    {
        return $this->db->where('token', $token)->get(self::TABLE_SESSIONS)->row();
    }

    public function delete_remember_token($token)
    {
        $this->db->where('token', $token)->delete(self::TABLE_SESSIONS);
    }

    public function delete_user_sessions($user_id)
    {
        $this->db->where('user_id', $user_id)->delete(self::TABLE_SESSIONS);
    }

    // ================================================================
    //  PASSWORD RESET
    // ================================================================

    public function set_reset_token($email)
    {
        $user = $this->db->where('email', $email)->where('is_active', 1)->get(self::TABLE_USERS)->row();
        if (!$user) return false;

        $token = bin2hex(random_bytes(32));
        $this->db->where('id', $user->id)->update(self::TABLE_USERS, [
            'token_reset'     => $token,
            'token_reset_exp' => date('Y-m-d H:i:s', time() + 3600), // 1 jam
        ]);

        return ['user' => $user, 'token' => $token];
    }

    public function get_user_by_reset_token($token)
    {
        return $this->db
                    ->where('token_reset', $token)
                    ->where('token_reset_exp >=', date('Y-m-d H:i:s'))
                    ->get(self::TABLE_USERS)
                    ->row();
    }

    public function reset_password($user_id, $new_password)
    {
        $this->db->where('id', $user_id)->update(self::TABLE_USERS, [
            'password'        => password_hash($new_password, PASSWORD_BCRYPT, ['cost' => 12]),
            'token_reset'     => null,
            'token_reset_exp' => null,
            'login_attempts'  => 0,
            'locked_until'    => null,
        ]);
    }

    // ================================================================
    //  CRUD USER
    // ================================================================

    public function get_all($filters = [])
    {
        $this->db->select('u.*, r.name AS role_name')
                 ->from(self::TABLE_USERS . ' u')
                 ->join(self::TABLE_ROLES . ' r', 'r.id = u.role_id');

        if (!empty($filters['role_id']))  $this->db->where('u.role_id', $filters['role_id']);
        if (isset($filters['is_active'])) $this->db->where('u.is_active', $filters['is_active']);
        if (!empty($filters['search'])) {
            $s = $this->db->escape_like_str($filters['search']);
            $this->db->group_start()
                     ->like('u.full_name', $s)
                     ->or_like('u.username', $s)
                     ->or_like('u.email', $s)
                     ->group_end();
        }

        $this->db->order_by('u.created_at', 'DESC');

        if (!empty($filters['limit'])) {
            $offset = $filters['offset'] ?? 0;
            $this->db->limit($filters['limit'], $offset);
        }

        return $this->db->get()->result();
    }

    public function count_all($filters = [])
    {
        $this->db->from(self::TABLE_USERS . ' u')
                 ->join(self::TABLE_ROLES . ' r', 'r.id = u.role_id');

        if (!empty($filters['role_id']))  $this->db->where('u.role_id', $filters['role_id']);
        if (isset($filters['is_active'])) $this->db->where('u.is_active', $filters['is_active']);
        if (!empty($filters['search'])) {
            $s = $this->db->escape_like_str($filters['search']);
            $this->db->group_start()
                     ->like('u.full_name', $s)
                     ->or_like('u.username', $s)
                     ->or_like('u.email', $s)
                     ->group_end();
        }

        return $this->db->count_all_results();
    }

    public function get_by_id($id)
    {
        return $this->db
                    ->select('u.*, r.name AS role_name')
                    ->from(self::TABLE_USERS . ' u')
                    ->join(self::TABLE_ROLES . ' r', 'r.id = u.role_id')
                    ->where('u.id', $id)
                    ->get()
                    ->row();
    }

    public function create($data)
    {
        $data['password']   = password_hash($data['password'], PASSWORD_BCRYPT, ['cost' => 12]);
        $data['created_at'] = date('Y-m-d H:i:s');
        $this->db->insert(self::TABLE_USERS, $data);
        return $this->db->insert_id();
    }

    public function update($id, $data)
    {
        if (isset($data['password']) && !empty($data['password'])) {
            $data['password'] = password_hash($data['password'], PASSWORD_BCRYPT, ['cost' => 12]);
        } else {
            unset($data['password']);
        }
        $data['updated_at'] = date('Y-m-d H:i:s');
        $this->db->where('id', $id)->update(self::TABLE_USERS, $data);
        return $this->db->affected_rows();
    }

    public function toggle_active($id)
    {
        $user = $this->get_by_id($id);
        if (!$user) return false;
        $this->db->where('id', $id)->update(self::TABLE_USERS, [
            'is_active' => $user->is_active ? 0 : 1,
        ]);
        return !$user->is_active;
    }

    public function delete($id)
    {
        // Soft-delete: cukup nonaktifkan, bukan hapus permanen
        return $this->toggle_active($id);
    }

    public function is_unique_email($email, $exclude_id = null)
    {
        $this->db->where('email', $email);
        if ($exclude_id) $this->db->where('id !=', $exclude_id);
        return $this->db->count_all_results(self::TABLE_USERS) === 0;
    }

    public function is_unique_username($username, $exclude_id = null)
    {
        $this->db->where('username', $username);
        if ($exclude_id) $this->db->where('id !=', $exclude_id);
        return $this->db->count_all_results(self::TABLE_USERS) === 0;
    }

    // ================================================================
    //  ROLES
    // ================================================================

    public function get_roles()
    {
        return $this->db->get(self::TABLE_ROLES)->result();
    }

    // ================================================================
    //  ACTIVITY LOG
    // ================================================================

    public function log_activity($user_id, $action, $description = null)
    {
        $this->db->insert(self::TABLE_LOGS, [
            'user_id'     => $user_id,
            'action'      => $action,
            'description' => $description,
            'ip_address'  => $this->input->ip_address(),
            'created_at'  => date('Y-m-d H:i:s'),
        ]);
    }

    public function get_activity_logs($limit = 50, $offset = 0)
    {
        return $this->db
                    ->select('al.*, u.full_name, u.username')
                    ->from(self::TABLE_LOGS . ' al')
                    ->join(self::TABLE_USERS . ' u', 'u.id = al.user_id', 'left')
                    ->order_by('al.created_at', 'DESC')
                    ->limit($limit, $offset)
                    ->get()
                    ->result();
    }
}