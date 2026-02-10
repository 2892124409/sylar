/**
 * @file singleton.h
 * @brief 单例模式封装
 * @details 支持多种单例模式：裸指针、智能指针
 *          模板参数 X 用于创建多个实例对应的 Tag
 *          模板参数 N 用于同一 Tag 创建多个实例索引
 */
#ifndef __SYLAR_SINGLETON_H__
#define __SYLAR_SINGLETON_H__

#include <memory>

namespace sylar
{

    namespace
    {

        template <class T, class X, int N>
        T &GetInstanceX()
        {
            static T v;
            return v;
        }

        template <class T, class X, int N>
        std::shared_ptr<T> GetInstancePtr()
        {
            static std::shared_ptr<T> v(new T);
            return v;
        }

    }

    /**
     * @brief 单例模式封装类（返回裸指针）
     * @tparam T 类型
     * @tparam X 为了创造多个实例对应的Tag
     * @tparam N 同一个Tag创造多个实例索引
     */
    template <class T, class X = void, int N = 0>
    class Singleton
    {
    public:
        /**
         * @brief 返回单例裸指针
         */
        static T *GetInstance()
        {
            static T v;
            return &v;
        }
    };

    /**
     * @brief 单例模式智能指针封装类
     * @tparam T 类型
     * @tparam X 为了创造多个实例对应的Tag
     * @tparam N 同一个Tag创造多个实例索引
     */
    template <class T, class X = void, int N = 0>
    class SingletonPtr
    {
    public:
        /**
         * @brief 返回单例智能指针
         */
        static std::shared_ptr<T> GetInstance()
        {
            static std::shared_ptr<T> v(new T);
            return v;
        }
    };

}

#endif
