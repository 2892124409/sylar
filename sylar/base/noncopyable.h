#ifndef __SYLAR_BASE_NONCOPYABLE_H__
#define __SYLAR_BASE_NONCOPYABLE_H__

namespace sylar
{

    /**
     * @brief 对象无法拷贝,赋值
     */
    class Noncopyable
    {
    public:
        /**
         * @brief 默认构造函数
         */
        Noncopyable() = default;

        /**
         * @brief 默认析构函数
         * 析构函数不使用虚函数是因为，本来也没打算用基类指针来操作派生类
         */
        ~Noncopyable() = default;

        /**
         * @brief 拷贝构造函数(禁用)
         */
        Noncopyable(const Noncopyable &) = delete;

        /**
         * @brief 赋值函数(禁用)
         */
        Noncopyable &operator=(const Noncopyable &) = delete;
    };

}

#endif
