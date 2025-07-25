//
// Copyright 2023 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
////////////////////////////////////////////////////////////////////////

/* ************************************************************************** */
/* **                                                                      ** */
/* ** This file is generated by a script.                                  ** */
/* **                                                                      ** */
/* ** Do not edit it directly (unless it is within a CUSTOM CODE section)! ** */
/* ** Edit hdSchemaDefs.py instead to make changes.                        ** */
/* **                                                                      ** */
/* ************************************************************************** */

#include "{{ INCLUDE_PATH }}/{{ FILE_NAME }}.h"

#include "pxr/imaging/hd/retainedDataSource.h"

{%- if IMPL_SCHEMA_INCLUDES is defined -%}
{%- for t in IMPL_SCHEMA_INCLUDES %}
#include "{{ t | expand }}.h"
{%- endfor -%}
{%- endif %}

#include "pxr/base/trace/trace.h"

// --(BEGIN CUSTOM CODE: Includes)--
{%- if 'Includes' in CUSTOM_CODE_IMPL %}
{{ CUSTOM_CODE_IMPL['Includes'] }}
{%- endif %}
// --(END CUSTOM CODE: Includes)--

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS({{SCHEMA_CLASS_NAME}}Tokens,
    {{ SCHEMA_CLASS_NAME|snake }}_TOKENS);

// --(BEGIN CUSTOM CODE: Schema Methods)--
{%- if 'Schema Methods' in CUSTOM_CODE_IMPL %}
{{ CUSTOM_CODE_IMPL['Schema Methods'] }}
{%- endif %}
// --(END CUSTOM CODE: Schema Methods)--

{%- if GENERIC_MEMBER is defined -%}
{%- set name, type_name, opt_dict = GENERIC_MEMBER -%}
{%- if opt_dict.get('GETTER', True) %}

TfTokenVector
{{ SCHEMA_CLASS_NAME }}::Get{{ name | capitalizeFirst }}Names()
{%- if VERSION_GUARD_CONST_GETTER %}
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{% else %} const
{% endif -%} {#- if VERSION_GUARD_CONST_GETTER -#}
{
    if (_container) {
        return _container->GetNames();
    } else {
        return {};
    }
}

{{ type_name}}{% if not type_name.endswith('Schema') %}Handle{% endif %}
{{ SCHEMA_CLASS_NAME }}::Get{{ name | capitalizeFirst }}(const TfToken &name)
{%- if VERSION_GUARD_CONST_GETTER %}
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{% else %} const
{% endif -%} {#- if VERSION_GUARD_CONST_GETTER -#}
{
    {%- if type_name.endswith('Schema') %}
    return {{type_name}}(
        _GetTypedDataSource<{{ type_name | underlyingDataSource}}>(name));
    {%- else %}
    return _GetTypedDataSource<{{ type_name | underlyingDataSource}}>(name);
    {%- endif %}
}
{%- endif -%} {# if opt_dict.get('GETTER', True) #}
{%- endif -%} {# if GENERIC_MEMBER is defiend #}

{%- if MEMBERS is defined -%}
{%- for name, type_name, opt_dict in MEMBERS -%}
{%- if opt_dict.get('GETTER', True) %}

{{ type_name}}{% if not type_name.endswith('Schema') %}Handle{% endif %}
{{ SCHEMA_CLASS_NAME }}::Get{{ name|capitalizeFirst }}()
{%- if VERSION_GUARD_CONST_GETTER %}
#if HD_API_VERSION >= 66
                                            const
#else
                                                 
#endif
{% else %} const
{% endif -%} {#- if VERSION_GUARD_CONST_GETTER -#}
{
    return {% if type_name.endswith('Schema') %}{{ type_name }}({% endif %}_GetTypedDataSource<{{type_name | underlyingDataSource}}>(
        {{SCHEMA_CLASS_NAME}}Tokens->{{ name }}){% if type_name.endswith('Schema') %}){% endif %};
}
{%- endif -%} {# if opt_dict.get('GETTER', True) #}
{%- endfor -%}
{%- endif -%} {# if MEMBERS is defined #}

{%- if GENERIC_MEMBER is defined %}

/*static*/
HdContainerDataSourceHandle
{{SCHEMA_CLASS_NAME}}::BuildRetained(
    const size_t count,
    const TfToken * const names,
    const HdDataSourceBaseHandle * const values)
{
    return HdRetainedContainerDataSource::New(count, names, values);
}
{%- else -%}
{%- if MEMBERS %}

/*static*/
HdContainerDataSourceHandle
{{ SCHEMA_CLASS_NAME }}::BuildRetained(
{%- for name, type_name, opt_dict in MEMBERS %}
        const {{type_name | underlyingDataSource}}Handle &{{ name }}{%if loop.last == False %},{% endif -%}
{%- endfor %}
)
{
    TfToken _names[{{MEMBERS|length}}];
    HdDataSourceBaseHandle _values[{{MEMBERS|length}}];

    size_t _count = 0;
{%- for name, type_name, opt_dict in MEMBERS %}

    if ({{name}}) {
        _names[_count] = {{SCHEMA_CLASS_NAME}}Tokens->{{name}};
        _values[_count++] = {{name}};
    }
{%- endfor %}
    return HdRetainedContainerDataSource::New(_count, _names, _values);
}

{%- for name, type_name, opt_dict in MEMBERS %}

{{ SCHEMA_CLASS_NAME }}::Builder &
{{ SCHEMA_CLASS_NAME }}::Builder::Set{{ name|capitalizeFirst }}(
    const {{ type_name | underlyingDataSource}}Handle &{{name}})
{
    _{{name}} = {{name}};
    return *this;
}
{%- endfor %}

HdContainerDataSourceHandle
{{ SCHEMA_CLASS_NAME }}::Builder::Build()
{
    return {{ SCHEMA_CLASS_NAME }}::BuildRetained(
{%- for name, type_name, opt_dict in MEMBERS %}
        _{{ name }}{%if loop.last == False %},{% endif -%}
{%- endfor %}
    );
}
{%- endif -%} {# else of if MEMBERS #}
{%- endif -%} {# else of if GENERIC_MEMBER is defined #}

{%- if SCHEMA_TOKEN is defined %}

/*static*/
{{ SCHEMA_CLASS_NAME }}
{{ SCHEMA_CLASS_NAME }}::GetFromParent(
        const HdContainerDataSourceHandle &fromParentContainer)
{
    return {{ SCHEMA_CLASS_NAME }}(
        fromParentContainer
        ? HdContainerDataSource::Cast(fromParentContainer->Get(
                {{SCHEMA_CLASS_NAME}}Tokens->{{SCHEMA_TOKEN}}))
        : nullptr);
}

/*static*/
const TfToken &
{{ SCHEMA_CLASS_NAME }}::GetSchemaToken()
{
    return {{SCHEMA_CLASS_NAME}}Tokens->{{SCHEMA_TOKEN}};
}
{%- if ADD_DEFAULT_LOCATOR is defined %}

/*static*/
const HdDataSourceLocator &
{{ SCHEMA_CLASS_NAME }}::GetDefaultLocator()
{
{%- if LOCATOR_PREFIX is defined %}
    static const HdDataSourceLocator locator =
        {{ LOCATOR_PREFIX }}.Append(GetSchemaToken());
{%- else %}
    static const HdDataSourceLocator locator(GetSchemaToken());
{%- endif %}
    return locator;
}
{%- endif -%}

{%- endif -%}


{%- if MEMBERS is defined -%}
{%- for name, type_name, opt_dict in MEMBERS -%}
{%- if opt_dict['ADD_LOCATOR'] %}

/* static */
const HdDataSourceLocator &
{{ SCHEMA_CLASS_NAME }}::Get{{ name |capitalizeFirst  }}Locator()
{
    static const HdDataSourceLocator locator =
        GetDefaultLocator().Append(
            {{SCHEMA_CLASS_NAME}}Tokens->{{name}});
    return locator;
}
{%- endif -%} {# if opt_dict['ADD_LOCATOR'] #}
{%- endfor -%}
{%- endif -%} {# if MEMBERS is defined #}

{%- if STATIC_LOCATOR_ACCESSORS is defined -%}
{%- for name, tokens in STATIC_LOCATOR_ACCESSORS %}

/*static*/
const HdDataSourceLocator &
{{ SCHEMA_CLASS_NAME }}::Get{{name|capitalizeFirst}}Locator()
{
    static const HdDataSourceLocator locator(
    {%- for t in tokens -%}
        {%if t is string %}{{SCHEMA_CLASS_NAME}}Tokens->{{ t }}{% else %}{{ t[0] }}SchemaTokens->{{ t[1] }}{% endif %}{%if loop.last == False %},{% endif -%}
    {%- endfor %}
    );
    return locator;
}
{%- endfor -%}
{%- endif -%} {# if STATIC_LOCATOR_ACCESSORS is defined #}

{%- if STATIC_TOKEN_DATASOURCE_BUILDERS is defined -%}
{%- for typeName, tokens in STATIC_TOKEN_DATASOURCE_BUILDERS %}

/*static*/
HdTokenDataSourceHandle
{{ SCHEMA_CLASS_NAME }}::Build{{typeName|capitalizeFirst}}DataSource(
    const TfToken &{{typeName}})
{
{% for token in tokens %}
    if ({{typeName}} == {{SCHEMA_CLASS_NAME}}Tokens->{{ token | tokenName }}) {
        static const HdRetainedTypedSampledDataSource<TfToken>::Handle ds =
            HdRetainedTypedSampledDataSource<TfToken>::New({{typeName}});
        return ds;
    }
{%- endfor %}
    // fallback for unknown token
    return HdRetainedTypedSampledDataSource<TfToken>::New({{typeName}});
}
{%- endfor -%}
{%- endif %} {# if STATIC_TOKEN_DATASOURCE_BUILDERS is defined #}

PXR_NAMESPACE_CLOSE_SCOPE
