#pragma once

#include <memory>

struct VskAst
{
    int m_int = 0;
    VskAst(const VskString& str)
    {
        m_int = std::atoi(str.c_str());
    }

    int to_int() const
    {
        return m_int;
    }
};
typedef std::shared_ptr<VskAst> VskAstPtr;

inline VskAstPtr vsk_eval_text(const VskString& str)
{
    return std::make_shared<VskAst>(str);
}
