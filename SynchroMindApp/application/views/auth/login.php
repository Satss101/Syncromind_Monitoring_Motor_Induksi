<!DOCTYPE html>
<html lang="id">
<head>
    <meta charset="UTF-8">
    <title><?= $title ?? 'Login' ?></title>
    <meta name="dicoding:email" content="khalimahmusaadah@gmail.com">

    <!-- Bootstrap CDN -->
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
</head>
<body class="bg-light">

<div class="container d-flex justify-content-center align-items-center" style="min-height:100vh;">
    <div class="card shadow" style="width:400px;">
        <div class="card-body">

            <h4 class="text-center mb-4">Login</h4>

            <!-- Flash message -->
            <?php if ($this->session->flashdata('error')): ?>
                <div class="alert alert-danger">
                    <?= $this->session->flashdata('error') ?>
                </div>
            <?php endif; ?>

            <?php if ($this->session->flashdata('success')): ?>
                <div class="alert alert-success">
                    <?= $this->session->flashdata('success') ?>
                </div>
            <?php endif; ?>

            <form action="<?= site_url('login/process') ?>" method="POST">

                <div class="mb-3">
                    <label>Username / Email</label>
                    <input type="text" name="login" class="form-control" required>
                </div>

                <div class="mb-3">
                    <label>Password</label>
                    <input type="password" name="password" class="form-control" required>
                </div>

                <div class="mb-3 form-check">
                    <input type="checkbox" name="remember_me" class="form-check-input" id="remember">
                    <label class="form-check-label" for="remember">Remember Me</label>
                </div>

                <button class="btn btn-primary w-100">Login</button>

            </form>

            <div class="text-center mt-3">
                <a href="<?= site_url('forgot-password') ?>">Lupa password?</a>
            </div>

        </div>
    </div>
</div>

</body>
</html>