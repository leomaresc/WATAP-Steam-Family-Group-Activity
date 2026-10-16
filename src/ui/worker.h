// Hilo de trabajo unico para operaciones de red e imagenes.
// Evita bloqueos de la interfaz y serializa las solicitudes (respeta los limites de Steam).
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace srp::ui {

class Worker {
public:
    using Task = std::function<void()>;

    Worker() = default;
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    void Post(Task task);

    // Detiene el hilo y espera a que finalice la tarea en curso.
    void Stop();

    bool Idle() const;

private:
    void Run();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    std::thread thread_;
    bool stopping_ = false;
    bool busy_ = false;
    bool started_ = false;
};

}  // namespace srp::ui
