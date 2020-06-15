# threadpool
Windows纯C自实现线程池  

【项目初衷】    
前几天在做一个epoll的任务处理框架，任务的处理放到线程池里去处理，由于是linux的代码，  
linux本身并没有提供线程池的库，因此在github上找到了一个实现(https://github.com/mbrossard/threadpool)，  
作者在项目中写道希望改进的一个方向是，提供一个windows版的，我认真看了代码中linux独有的东西包括以下几个：     
1.pthread_mutex_t 互斥锁   
2.pthread_cond_t 条件量  
3.pthread_t 创建线程的函数  

于是我开始寻找win的代替代码，互斥锁在windows上有critical section可以代替，create_thread在windows上  
有_beginthread可以代替，唯一没有思路的是pthread_cond_t，pthread_cond_t一般是和互斥锁一起使用解决   
可能存在的死锁问题，而死锁出现的原因是在获取到互斥锁的线程在进入关键段后陷入等待（等待其他条件，比如bool的翻转等），  
只要解决这个等待的问题，那么pthread_cond_t就不是必要的。  

在原来的代码中，等待的原因是，拿到互斥锁之后，任务队列可能为0，于是开始等待新的任务到来，我想可不可以任务队列不为零   
的时候才让线程难道互斥锁：   
原来的代码逻辑：加锁 -> 队列空 -> 等待 -> 队列不为空 -> 处理 -> 解锁   
我的思路是：等待队列不为空 -> 加锁 -> 处理 -> 解锁   
而且等待的过程中要让线程挂起，决不能以某种轮询的方式浪费CPU时间   

经过查阅发现windows的信号量内核对象(Semaphore)配合WaitForSingleObject刚好满足我的要求，思路是这样的，   
当有新的任务加入队里的时候，调用ReleaseSemaphore让Semaphore自加1，而在WaitForSingleObject(Semaphore)会自减1，   
如果WaitForSingleObject(Semaphore)时，Semaphore为空，那么调用线程将陷入等待，之后新的任务到来调用了ReleaseSemaphore   
才会唤起等待的进程，其中ReleaseSemaphore和WaitForSingleObject等都是原子操作，这保证了不同线程在对Semaphore操作之后   
结果的正确性，从保证让线程在拿到锁之前，任务队列不为空。   

这个方案的本质其实是，设置一个变量体现任务数，过来一个任务+1，处理一个任务-1，只有任务数不为0的时候，才允许线程拿到锁，   
这个任务数就是Semaphore变量，而ReleaseSemaphore和WaitForSingleObject保证了+1和-1的原子性，WaitForSingleObject还   
解决了等待时的挂起问题，从巧妙地解决了上面提出的问题。  
