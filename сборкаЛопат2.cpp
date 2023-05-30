#include <iostream>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <functional>

const int MAX_STORAGE_CAPACITY = 100;
const int MAX_DEALERS = 10;
const int MAX_ASSEMBLERS = 5;

// Класс детали, из которых состоит лопата
class Detail {
public:
    Detail(int id) : m_id(id) {}
    int getId() const { return m_id; }
private:
    int m_id;
};

// Класс деревянного черенка
class Handle : public Detail {
public:
    Handle(int id) : Detail(id) {}
};

// Класс металлического штыка
class Blade : public Detail {
public:
    Blade(int id) : Detail(id) {}
};

// Класс лопаты
class Shovel {
public:
    Shovel(int id, Handle* handle, Blade* blade) : m_id(id), m_handle(handle), m_blade(blade) {}
    int getId() const { return m_id; }
    Handle* getHandle() const { return m_handle; }
    Blade* getBlade() const { return m_blade; }
private:
    int m_id;
    Handle* m_handle;
    Blade* m_blade;
};

// Класс склада деталей
class DetailStorage {
public:
    DetailStorage(int maxCapacity) : m_maxCapacity(maxCapacity) {}

    bool isFull() const {
        return m_details.size() >= m_maxCapacity;
    }

    bool isEmpty() const {
        return m_details.empty();
    }

    void addDetail(Detail* detail) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_details.push(detail);
        m_notEmpty.notify_one();
    }

    Detail* getDetail() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait(lock, [this]() { return !m_details.empty(); });
        Detail* detail = m_details.front();
        m_details.pop();
        m_notFull.notify_one();
        return detail;
    }

private:
    int m_maxCapacity;
    std::queue<Detail*> m_details;
    std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
};

// Класс склада готовых лопат
class ShovelStorage {
public:
    ShovelStorage(int maxCapacity) : m_maxCapacity(maxCapacity) {}

    bool isFull() const {
        return m_shovels.size() >= m_maxCapacity;
    }

    bool isEmpty() const {
        return m_shovels.empty();
    }

    void addShovel(Shovel* shovel) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_shovels.push_back(shovel);
        m_notEmpty.notify_one();
    }

    Shovel* getShovel() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait(lock, [this]() { return !m_shovels.empty(); });
        Shovel* shovel = m_shovels.back();
        m_shovels.pop_back();
        m_notFull.notify_one();
        return shovel;
    }

    std::vector<Shovel*> getShovels() const {
        return m_shovels;
    }

private:
    int m_maxCapacity;
    std::vector<Shovel*> m_shovels;
    std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
};

// Класс контроллера склада готовых лопат
class ShovelStorageController {
public:
    ShovelStorageController(ShovelStorage* storage, DetailStorage* handleStorage,
        DetailStorage* bladeStorage, int productionTime, int maxShovels)
        : m_storage(storage), m_handleStorage(handleStorage), m_bladeStorage(bladeStorage), m_productionTime(productionTime), m_maxShovels(maxShovels) {}

    void start() {
        while (true) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_notEmpty.wait(lock, [this]() { return !m_storage->isEmpty(); });
            Shovel* shovel = m_storage->getShovel();
            m_notFull.notify_all();
            logShovel(shovel, "sold");
            if (m_storage->isFull()) {
                int numShovelsToAdd = m_maxShovels - m_storage->getShovels().size();
                for (int i = 0; i < numShovelsToAdd; i++) {
                    Shovel* newShovel = createShovel();
                    m_storage->addShovel(newShovel);
                    logShovel(newShovel, "produced");
                }
            }
        }
    }

private:
    Shovel* createShovel() {
        Handle* handle = m_handleStorage->getDetail<Handle*>();
        Blade* blade = m_bladeStorage->getDetail<Blade*>();
        Shovel* shovel = new Shovel(getNextShovelId(), handle, blade);
        return shovel;
    }

    int getNextShovelId() {
        static int nextId = 1;
        return nextId++;
    }

    template <typename T>
    T* getDetail() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait(lock, [this]() { return !m_details.empty(); });
        Detail* detail = m_details.front();
        m_details.pop();
        m_notFull.notify_one();
        return dynamic_cast<T*>(detail);
    }

    void logShovel(Shovel* shovel, const std::string& action) {
        std::ofstream logFile("shovel_log.txt", std::ios::app);
        logFile << "Dealer " << m_dealerNum << ": Shovel " << shovel->getId()
            << " (Handle: " << shovel->getHandle()->getId()
            << ", Blade: " << shovel->getBlade()->getId()
            << ") " << action << std::endl;
        logFile.close();
    }

private:
    ShovelStorage* m_storage;
    DetailStorage* m_handleStorage;
    DetailStorage* m_bladeStorage;
    int m_productionTime;
    int m_maxShovels;
    int m_dealerNum;
    std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
};

// Класс поставщика деталей
class DetailSupplier {
public:
    DetailSupplier(DetailStorage* storage, int supplyTime)
        : m_storage(storage), m_supplyTime(supplyTime) {}

    void start() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_supplyTime));
            if (m_storage->isFull()) {
                continue;
            }
            Detail* detail = createDetail();
            m_storage->addDetail(detail);
        }
    }

private:
    Detail* createDetail() {
        static int nextDetailId = 1;
        Detail* detail = new Detail(nextDetailId++);
        return detail;
    }

private:
    DetailStorage* m_storage;
    int m_supplyTime;
};

// Класс сборщика лопат
class ShovelAssembler {
public:
    ShovelAssembler(ShovelStorage* storage, DetailStorage* handleStorage,
        DetailStorage* bladeStorage, int assemblyTime)
        : m_storage(storage), m_handleStorage(handleStorage), m_bladeStorage(bladeStorage), m_assemblyTime(assemblyTime) {}

    void start() {
        while (true) {
            Handle* handle = m_handleStorage->getDetail<Handle>();
            Blade* blade = m_bladeStorage->getDetail<Blade>();
            Shovel* shovel = new Shovel(getNextShovelId(), handle, blade);
            std::this_thread::sleep_for(std::chrono::milliseconds(m_assemblyTime));
            m_storage->addShovel(shovel);
            logShovel(shovel, "assembled");
        }
    }

private:
    int getNextShovelId() {
        static int nextId = 1;
        return nextId++;
    }

    void logShovel(Shovel* shovel, const std::string& action) {
        std::ofstream logFile("shovel_log.txt", std::ios::app);
        logFile << "Assembler: Shovel " << shovel->getId()
            << " (Handle: " << shovel->getHandle()->getId()
            << ", Blade: " << shovel->getBlade()->getId()
            << ") " << action << std::endl;
        logFile.close();
    }
    ShovelStorage* m_storage;
    DetailStorage* m_handleStorage;
    DetailStorage* m_bladeStorage;
    int m_assemblyTime;
};

// Класс пула потоков
class ThreadPool {
public:
    ThreadPool(int numThreads) : m_stop(false) {
        for (int i = 0; i < numThreads; i++) {
            m_threads.emplace_back(std::thread([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_condition.wait(lock, [this]() { return m_stop || !m_tasks.empty(); });
                        if (m_stop && m_tasks.empty()) {
                            return;
                        }
                        task = std::move(m_tasks.front());
                        m_tasks.pop();
                    }
                    task();
                }
                }));
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_condition.notify_all();
        for (std::thread& thread : m_threads) {
            thread.join();
        }
    }

    template <typename T, typename... Args>
    void enqueue(T&& t, Args&&... args) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_tasks.emplace(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
        m_condition.notify_one();
    }

private:
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>>m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_stop;
};

int main() {
    DetailStorage handleStorage(MAX_STORAGE_CAPACITY);
    DetailStorage bladeStorage(MAX_STORAGE_CAPACITY);
    ShovelStorage shovelStorage(MAX_STORAGE_CAPACITY);
    ThreadPool pool(MAX_DEALERS + MAX_ASSEMBLERS);

    // Создаем детейл-поставщиков и добавляем их задачи в пул потоков
    for (int i = 0; i < MAX_STORAGE_CAPACITY; i++) {
        DetailSupplier* handleSupplier = new DetailSupplier(&handleStorage, 100);
        DetailSupplier* bladeSupplier = new DetailSupplier(&bladeStorage, 150);
        pool.enqueue(&DetailSupplier::start, handleSupplier);
        pool.enqueue(&DetailSupplier::start, bladeSupplier);
    }

    // Создаем сборщиков лопат и добавляем их задачи в пул потоков
    for (int i = 0; i < MAX_ASSEMBLERS; i++) {
        ShovelAssembler* assembler = new ShovelAssembler(&shovelStorage, &handleStorage, &bladeStorage, 250);
        pool.enqueue(&ShovelAssembler::start, assembler);
    }

    // Создаем контроллеры склада готовых лопат и добавляем их задачи в пул потоков
    for (int i = 0; i < MAX_DEALERS; i++) {
        ShovelStorageController* controller = new ShovelStorageController(&shovelStorage, &handleStorage, &bladeStorage, 500, 50);
        pool.enqueue(&ShovelStorageController::start, controller);
    }

    // Ожидаем завершения работы всех задач в пуле потоков
    std::this_thread::sleep_for(std::chrono::seconds(60));

    return 0;
}