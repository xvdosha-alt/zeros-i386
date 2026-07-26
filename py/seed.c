#include "seed.h"
#include "fs.h"
#include "runtime.h"
#include "host_io.h"
#include "util.h"
#include "object.h"

static void put_file(const char *name, const char *src)
{
    int fd = mpfs_open(name, MPFS_O_WRITE | MPFS_O_CREATE | MPFS_O_TRUNC);
    if (fd < 0)
        return;
    mpfs_write(fd, src, mp_strlen(src));
    mpfs_close(fd);
}

void mp_seed_stdlib(void)
{
    static char util[] =
        "def clamp(x, lo, hi):\n"
        "    if x < lo:\n"
        "        return lo\n"
        "    if x > hi:\n"
        "        return hi\n"
        "    return x\n"
        "\n"
        "def sum(xs):\n"
        "    t = 0\n"
        "    for x in xs:\n"
        "        t = t + x\n"
        "    return t\n";
    static char app[] =
        "import util\n"
        "\n"
        "def greet(name):\n"
        "    return \"hi \" + name\n"
        "\n"
        "def demo():\n"
        "    xs = range(5)\n"
        "    print(greet(\"minipy\"))\n"
        "    print(sum(xs))\n"
        "    print(clamp(10, 0, 3))\n"
        "    f = open(\"out.txt\", \"w\")\n"
        "    write(f, \"ok\")\n"
        "    close(f)\n"
        "    append(xs, 99)\n"
        "    return len(xs)\n";
    static char mainpy[] =
        "import app\n"
        "print(\"zero-overhead boot\")\n"
        "print(demo())\n";
    put_file("util.py", util);
    put_file("app.py", app);
    put_file("main.py", mainpy);
}

int mp_try_run_main(MpRuntime *rt)
{
    static char src[MPFS_FILE_MAX];
    int fd;
    int n;
    MpObject *res = NULL;

    if (!mpfs_exists("main.py"))
        return 0;
    fd = mpfs_open("main.py", MPFS_O_READ);
    if (fd < 0)
        return 0;
    n = mpfs_read(fd, src, sizeof(src) - 1);
    mpfs_close(fd);
    if (n < 0)
        return 0;
    src[n] = 0;
    if (!mp_runtime_exec(rt, src, &res)) {
        mp_write("main.py: ");
        mp_writeln(rt->err);
        return 0;
    }
    return 1;
}
