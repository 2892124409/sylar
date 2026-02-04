#ifndef __SYLAR_BASE_CONFIG_H__
#define __SYLAR_BASE_CONFIG_H__

#include <memory>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "sylar/log/logger.h"
#include "util.h"

namespace sylar
{

    /**
     * @brief 配置变量基类
     */
    class ConfigVarBase
    {
    public:
        typedef std::shared_ptr<ConfigVarBase> ptr;

        /**
         * @brief 构造函数
         * @param[in] name 配置参数名称
         * @param[in] description 配置参数描述
         */
        ConfigVarBase(const std::string &name, const std::string &description = "")
            : m_name(name), m_description(description)
        {
            // 名字统一转小写，方便查找
            std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
        }

        virtual ~ConfigVarBase() {}

        const std::string &getName() const { return m_name; }
        const std::string &getDescription() const { return m_description; }

        /**
         * @brief 转成字符串
         */
        virtual std::string toString() = 0;

        /**
         * @brief 从字符串初始化
         */
        virtual bool fromString(const std::string &val) = 0;

        /**
         * @brief 获取配置参数类型名称
         */
        virtual std::string getTypeName() const = 0;

    protected:
        std::string m_name;        // 配置参数名称
        std::string m_description; // 配置参数描述
    };

    /**
     * @brief 类型转换模板类 (词法转换)
     * F: From Type, T: To Type
     */
    template <class F, class T>
    class LexicalCast
    {
    public:
        T operator()(const F &v)
        {
            return boost::lexical_cast<T>(v);
        }
    };

    /**
     * @brief 配置参数模板类
     * T: 参数类型
     * FromStr: 从字符串转T的转换器
     * ToStr: 从T转字符串的转换器
     */
    template <class T, class FromStr = LexicalCast<std::string, T>, class ToStr = LexicalCast<T, std::string>>
    class ConfigVar : public ConfigVarBase
    {
    public:
        typedef std::shared_ptr<ConfigVar> ptr;
        typedef std::function<void(const T &old_value, const T &new_value)> on_change_cb;

        ConfigVar(const std::string &name, const T &default_value, const std::string &description = "")
            : ConfigVarBase(name, description), m_val(default_value)
        {
        }

        /**
         * @brief 将变量值转成字符串
         */
        std::string toString() override
        {
            try
            {
                return ToStr()(m_val);
            }
            catch (std::exception &e)
            {
                // 这里以后可以加日志记录错误
            }
            return "";
        }

        /**
         * @brief 从字符串反序列化回变量值
         */
        bool fromString(const std::string &val) override
        {
            try
            {
                setValue(FromStr()(val));
            }
            catch (std::exception &e)
            {
                // 这里以后可以加日志记录错误
            }
            return false;
        }

        const T getValue() const { return m_val; }

        void setValue(const T &v)
        {
            if (v == m_val)
            {
                return;
            }
            // 触发变更回调
            for (auto &i : m_cbs)
            {
                i.second(m_val, v);
            }
            m_val = v;
        }

        std::string getTypeName() const override { return typeid(T).name(); }

    private:
        /**
         * @brief 实际存储的配置变量值
         */
        T m_val;

        /**
         * @brief 变更回调函数组
         * key: 回调函数的唯一标识 (通常使用hash或自增id)
         * value: 回调函数内容 (参数为旧值和新值)
         * 作用：当配置值发生变化时，通知所有注册了该事件的代码进行热更新处理
         */
        std::map<uint64_t, on_change_cb> m_cbs;
    };
    
    /**
     * @brief Config管理类
     */
    class Config
    {
    public:
        /**
         * @brief 配置变量映射表
         * key: 配置变量的名称 (必须全局唯一)
         * value: 指向配置变量基类的智能指针
         * 作用：作为全局配置仓库，存储系统中所有的配置项，以便统一进行查找和 YAML 加载。
         */
        typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;

        /**
         * @brief 查找配置参数，存在则返回，不存在则创建
         */
        template <class T>
        static typename ConfigVar<T>::ptr Lookup(const std::string &name,
                                                 const T &default_value, const std::string &description = "")
        {
            auto it = GetDatas().find(name);
            if (it != GetDatas().end())
            {
                auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
                if (tmp)
                {
                    return tmp;
                }
                else
                {
                    // 名字冲突但类型不同
                    return nullptr;
                }
            }

            // 校验名字合法性，sylar约定只允许小写字母、数字、点
            if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos)
            {
                // 这里可以记录错误日志
                throw std::invalid_argument(name);
            }

            typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
            GetDatas()[name] = v;
            return v;
        }

        /**
         * @brief 查找配置参数（只读）
         */
        template <class T>
        static typename ConfigVar<T>::ptr Lookup(const std::string &name)
        {
            auto it = GetDatas().find(name);
            if (it == GetDatas().end())
            {
                return nullptr;
            }
            return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
        }

        /**
         * @brief 获取所有的配置项
         */
        static ConfigVarMap &GetDatas()
        {
            static ConfigVarMap s_datas;
            return s_datas;
        }

    private:
        // 这里为了防止静态变量初始化顺序问题，通常使用函数内部静态变量
        // static ConfigVarMap s_datas;
    };

}

#endif
