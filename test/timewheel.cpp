#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <thread>
#include <unistd.h>

using ReleaseFunc = std::function<void()>; // 定时器释放后的清理函数
using TaskFunc = std::function<void()>;    // 定时器到期执行的任务


// 单个定时任务
// 利用 shared_ptr 生命周期：最后一个 shared_ptr 释放时析构并执行任务
class TimeTask
{
private:
    uint64_t _id;          // 定时器任务ID
    uint32_t _timeout;     // 超时时间
    bool _canceled;         // 任务是否被取消
    TaskFunc _task_cb;     // 到期后执行的任务
    ReleaseFunc _release;  // 任务结束后的清理函数

public:
    // 创建定时任务
    TimeTask(uint64_t id, uint32_t timeout, TaskFunc task)
        : _id(id),
          _timeout(timeout),
          _task_cb(task),
            _canceled(false)
    {
    }

    // 对象销毁代表定时器真正到期
    ~TimeTask()
    {
        if(!_canceled)
        _task_cb(); // 执行定时任务
        _release(); // 从 TimerWheel 中清除任务记录
    }
    void Cancel()
    {
        _canceled = true;
    }

    // 设置释放时的清理函数
    void SetRelease(const ReleaseFunc &cb)
    {
        _release = cb;
    }

    // 获取原本设置的超时时间，用于刷新定时器
    uint32_t DelayTime()
    {
        return _timeout;
    }
};


// 单层时间轮
// _wheel 保存任务，_tick 每次向前移动一个槽
class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimeTask>;
    using PtrTask = std::shared_ptr<TimeTask>;

    int _capacity;  // 时间轮槽数量
    // 时间轮槽，每个槽可以保存多个定时任务
    std::vector<std::vector<PtrTask>> _wheel;

    int _tick;      // 当前时间轮走到的位置

    // ID -> 定时任务
    // 使用 weak_ptr，不增加引用计数，不影响任务析构
    std::unordered_map<uint64_t, WeakTask> _timers;


private:
    // 定时任务结束后，从哈希表中删除任务记录
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }


public:
    // 默认创建 60 个槽的时间轮
    TimerWheel()
        : _capacity(60),
          _tick(0),
          _wheel(_capacity)
    {
    }


    // 添加定时任务
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        // 创建任务，由 shared_ptr 管理生命周期
        PtrTask pt(new TimeTask(id, delay, cb));

        // 任务析构后自动从 _timers 中删除
        pt->SetRelease(
            std::bind(&TimerWheel::RemoveTimer, this, id)
        );

        // 根据当前位置和延迟时间计算任务所在槽
        int pos = (_tick + delay) % _capacity;

        // 时间轮中的 shared_ptr 真正控制任务生命周期
        _wheel[pos].push_back(pt);

        // 建立 ID -> 任务 的映射
        // weak_ptr 不增加引用计数
        _timers[id] = WeakTask(pt);
    }


    // 刷新定时任务：从当前时间重新计算超时时间
    void TimerRefresh(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it != _timers.end())
        {
            // weak_ptr 转换为 shared_ptr
            PtrTask pt = it->second.lock();

            if (!pt)
                return;

            // 获取原来的超时时间
            int delay = pt->DelayTime();

            // 从当前 tick 开始重新计算槽位置
            int pos = (_tick + delay) % _capacity;

            /*
                将同一个 shared_ptr 再放入新的槽。

                旧槽到期：
                    引用计数减1，但不会归0，因此任务不会执行。

                新槽到期：
                    最后一个 shared_ptr 被释放，
                    TimeTask 析构，真正执行任务。
            */
            _wheel[pos].push_back(pt);
        }
    }

    void TimerCancel(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it != _timers.end())
        {
            // weak_ptr 转换为 shared_ptr
            PtrTask pt = it->second.lock();

            if (!pt)
                return;

            pt->Cancel();
        }
    }

    // 推动时间轮运行一次
    // 如果每秒调用一次，就表示时间向前走1秒
    void RuntimerTask()
    {
        // 时间轮向前移动，走到末尾后回到0
        _tick = (_tick + 1) % _capacity;

        /*
            清空当前槽，释放其中的 shared_ptr。

            没刷新过：
                引用计数归0 -> TimeTask析构 -> 执行任务

            刷新过：
                新槽还有 shared_ptr -> 不析构
        */
        _wheel[_tick].clear();
    }
};
class Test{
    public:
    Test(){std::cout<<"构造"<<std::endl;}
    ~Test(){std::cout<<"析构"<<std::endl;}
};
void DelTest(Test* t)
{
    delete t;
}
int main()
{

    TimerWheel tw;
    Test *t =new Test();
    tw.TimerAdd(1, 3, std::bind(DelTest, t));
    for(int i=0;i<5;i++)
    {
        sleep(1);
        tw.TimerRefresh(1);
        tw.RuntimerTask();
        std::cout<< "刷新一次定时任务，重新需要5s之后执行" << std::endl;
    
    }
    while (true)
    {
        sleep(1);
        tw.RuntimerTask();
        std::cout<< "时间轮走动一次" << std::endl;
    }
    return 0;
}