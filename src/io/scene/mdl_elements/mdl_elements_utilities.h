/***************************************************************************************************
 * Copyright (c) 2012-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************************************/
/// \file
/// \brief      Module-internal utilities related to MDL scene elements.

#ifndef IO_SCENE_MDL_ELEMENTS_MDL_ELEMENTS_UTILITIES_H
#define IO_SCENE_MDL_ELEMENTS_MDL_ELEMENTS_UTILITIES_H

#include <string>
#include <vector>
#include <list>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <base/data/db/i_db_tag.h>
#include <mi/base/interface_implement.h>
#include <mi/mdl/mdl_code_generators.h>
#include <mi/mdl/mdl_definitions.h>
#include <mi/mdl/mdl_modules.h>
#include <mi/neuraylib/ifunction_definition.h>
#include <mi/neuraylib/imdl_loading_wait_handle.h>

#include "i_mdl_elements_expression.h"
#include "i_mdl_elements_module.h"
#include "i_mdl_elements_type.h"
#include "i_mdl_elements_value.h"

namespace mi {
    namespace neuraylib { class IReader; }
    namespace base      { struct Uuid; }
    namespace mdl       { class IMDL; }
}

namespace MI {

    namespace DB { class Transaction; class Tag; }

namespace MDL {

class Mdl_compiled_material;
class Mdl_module_wait_queue;
class Name_mangler;

// ********** Conversion from mi::mdl to mi::neuraylib *********************************************

/// Converts mi::mdl::IDefinition::Semantics to mi::neuraylib::IFunction_definition::Semantics.
///
/// Some values cannot appear here. Such values are mapped to DS_UNKNOWN after an assertion.
mi::neuraylib::IFunction_definition::Semantics mdl_semantics_to_ext_semantics(
    mi::mdl::IDefinition::Semantics semantic);

/// Converts mi::neuraylib::IFunction_definition::Semantics to mi::mdl::IDefinition::Semantics.
mi::mdl::IDefinition::Semantics ext_semantics_to_mdl_semantics(
    mi::neuraylib::IFunction_definition::Semantics);

/// Converts mi::mdl::IDefinition::Semantics to mi::neuraylib::IAnnotation_definition::Semantics.
///
/// Some values cannot appear here. Such values are mapped to DS_UNKNOWN after an assertion.
mi::neuraylib::IAnnotation_definition::Semantics mdl_semantics_to_ext_annotation_semantics(
    mi::mdl::IDefinition::Semantics semantic);

// ********** Conversion from mi::mdl to MI::MDL ***************************************************

/// A vector of mi::mdl::DAG_node pointer.
typedef std::vector<const mi::mdl::DAG_node*> Mdl_annotation_block;

/// A vector of vectors of mi::mdl::DAG_node pointer.
typedef std::vector<std::vector<const mi::mdl::DAG_node*> > Mdl_annotation_block_vector;

/// Converts mi::mdl::IType to MI::MDL::IType.
///
/// \param tf                   The type factory to use.
/// \param type                 The type to convert.
/// \param annotations          For enums and structs the annotations of the enum/struct itself,
///                             otherwise \c NULL.
/// \param member_annotations   For enums and structs the annotations of the values/fields,
///                             otherwise \c NULL.
const IType* mdl_type_to_int_type(
    IType_factory* tf,
    const mi::mdl::IType* type,
    const Mdl_annotation_block* annotations = 0,
    const Mdl_annotation_block_vector* member_annotations = 0);


/// Converts mi::mdl::IType_enum to MI::MDL::IType_enum and checks,
/// if the type conflicts with an existing type.
///
/// \param tf                   The type factory to use.
/// \param type                 The type to convert.
/// \param annotations          The annotations of the enum.
/// \param member_annotations   The annotations of the values/fields.
bool mdl_type_enum_to_int_type_test(
    IType_factory* tf,
    const mi::mdl::IType_enum* type,
    const Mdl_annotation_block* annotations = 0,
    const Mdl_annotation_block_vector* member_annotations = 0);

/// Converts mi::mdl::IType_struct to MI::MDL::IType_struct and checks,
/// if the type conflicts with an existing type.
///
/// \param tf                   The type factory to use.
/// \param type                 The type to convert.
/// \param annotations          The annotations of the enum.
/// \param member_annotations   The annotations of the values/fields.
bool mdl_type_struct_to_int_type_test(
    IType_factory* tf,
    const mi::mdl::IType_struct* type,
    const Mdl_annotation_block* annotations = 0,
    const Mdl_annotation_block_vector* member_annotations = 0);

/// Converts mi::mdl::IType to MI::MDL::IType.
///
/// Template version of the function above.
template <class T>
const T* mdl_type_to_int_type(
    IType_factory* tf,
    const mi::mdl::IType* type,
    const Mdl_annotation_block* annotations = 0,
    const Mdl_annotation_block_vector* member_annotations = 0)
{
    mi::base::Handle<const IType> ptr_type(
        mdl_type_to_int_type( tf, type, annotations, member_annotations));
    if( !ptr_type)
        return 0;
    return static_cast<const T*>( ptr_type->get_interface( typename T::IID()));
}
/// Converts MI::MDL::IType to mi::mdl::IType.
const mi::mdl::IType* int_type_to_mdl_type(
    const IType *type,
    mi::mdl::IType_factory &tf);

/// Converts mi::mdl::IValue to MI::MDL::IValue.
///
/// \param vf                     The value factory to use.
/// \param transaction            The DB transaction to use.
/// \param type_int               The expected type of the return value. Used to control whether
///                               array arguments are converted to immediate-sized or deferred-sized
///                               arrays. If \c NULL, the type of \p value is used.
/// \param value                  The value to convert.
/// \param module_filename        The filename of the module (used for string-based resources with
///                               relative filenames).
/// \param module_name            The fully-qualified MDL module name.
/// \param load_resources         If true, resources are loaded into the database.
/// \return                       The converted value, or \c NULL in case of failures.
IValue* mdl_value_to_int_value(
    IValue_factory* vf,
    DB::Transaction* transaction,
    const IType* type_int,
    const mi::mdl::IValue* value,
    const char* module_filename,
    const char* module_name,
    bool load_resources);

/// Converts mi::mdl::IValue to MI::MDL::IValue.
///
/// Template version of the function above.
template <class T>
IValue* mdl_value_to_int_value(
    IValue_factory* vf,
    DB::Transaction* transaction,
    const IType* type_int,
    const mi::mdl::IValue* value,
    const char* module_filename,
    const char* module_name,
    bool load_resources)
{
    mi::base::Handle<IValue> ptr_value(
        mdl_value_to_int_value(
            vf, transaction, type_int, value, module_filename, module_name, load_resources));
    if( !ptr_value)
        return 0;
    return static_cast<T*>( ptr_value->get_interface( typename T::IID()));
}

/// Converts mi::mdl::DAG_node to MI::MDL::IExpression and MI::MDL::IAnnotation
class Mdl_dag_converter
{
public:

    /// Constructor.
    ///
    /// \param ef                     The expression factory to use.
    /// \param transaction            The DB transaction to use.
    /// \param immutable_callees      \c true for defaults, \c false for arguments
    /// \param create_direct_calls    \c true creates EK_DIRECT_CALLs, \c false creates EK_CALLs
    /// \param module_filename        The filename of the module (used for string-based resources
    ///                               with relative filenames).
    /// \param module_name            The fully-qualified MDL module name.
    /// \param prototype_tag          The prototype_tag if relevant.
    /// \param load_resources         \c True, if resources are supposed to be loaded into the DB
    /// \param user_modules_seen      If non - \c NULL, visited user module tags and identifiers
    ///                               are put here.
    Mdl_dag_converter(
        IExpression_factory* ef,
        DB::Transaction* transaction,
        bool immutable_callees,
        bool create_direct_calls,
        const char* module_filename,
        const char* module_name,
        DB::Tag prototype_tag,
        bool load_resources,
        std::set<Mdl_tag_ident>* user_modules_seen);

    /// Converts mi::mdl::DAG_node to MI::MDL::IExpression.
    ///
    /// \param node       The DAG node to convert.
    /// \param type_int   The expected type of the return value. Used to control whether
    ///                   array arguments are converted to immediate-sized or deferred-sized
    ///                   arrays. If \c NULL, the type of \p value is used.
    ///
    /// \return           The converted expression, or \c NULL in case of failures.
    IExpression* mdl_dag_node_to_int_expr(
        const mi::mdl::DAG_node* node,
        const IType* type_int) const;

    /// Converts a vector of mi::mdl::DAG_node pointers to MI::MDL::IAnnotation_block.
    IAnnotation_block* mdl_dag_node_vector_to_int_annotation_block(
        const Mdl_annotation_block& mdl_annotations,
        const char* qualified_name) const;

private:

    /// Converts mi::mdl::DAG_call to MI::MDL::IExpression.
    /// (creates IExpression_direct_call)
    IExpression* mdl_call_to_int_expr_direct(
        const mi::mdl::DAG_call* call, bool use_parameter_type) const;

    /// Converts mi::mdl::DAG_call to MI::MDL::IExpression.
    /// (creates IExpression_call)
    IExpression* mdl_call_to_int_expr_indirect(
        const mi::mdl::DAG_call* call, bool use_parameter_type) const;

    /// Converts mi::mdl::DAG_call to MI::MDL::IAnnotation.
    IAnnotation* mdl_dag_call_to_int_annotation(
        const mi::mdl::DAG_call* call, const char* qualified_name) const;

    /// Converts mi::mdl::DAG_node string and string array annotations
    /// to MI::MDL::IExpression, thereby translating strings according to the
    /// current locale.
    IExpression* mdl_dag_node_to_int_expr_localized(
        const mi::mdl::DAG_node* argument,
        const mi::mdl::DAG_call* call,
        const IType* type_int,
        const char* qualified_name) const;

private:
    
    mi::base::Handle<IExpression_factory> m_ef;
    mi::base::Handle<IValue_factory> m_vf;
    mi::base::Handle<IType_factory> m_tf;

    DB::Transaction* m_transaction;
    bool m_immutable_callees;
    bool m_create_direct_calls;

    const char* m_module_filename;
    const char* m_module_name;
    DB::Tag     m_prototype_tag; ///< The prototype of the mat. def. converted if relevant
    bool        m_load_resources;
    mutable std::set<Mdl_tag_ident>*
                m_user_modules_seen;
};

// ********** Conversion from MI::MDL to mi::mdl ***************************************************

/// Converts MI::MDL::IValue to mi::mdl::IValue.
const mi::mdl::IValue* int_value_to_mdl_value(
    DB::Transaction* transaction,
    mi::mdl::IValue_factory* vf,
    const mi::mdl::IType* mdl_type,
    const IValue* value);

/// Converts MI::MDL::IValue to mi::mdl::IExpression_literal.
const mi::mdl::IExpression_literal* int_value_to_mdl_literal(
    DB::Transaction* transaction,
    mi::mdl::IModule* module,
    const mi::mdl::IType* mdl_type,
    const IValue* value);

/// Converts MI::MDL::IExpression to mi::mdl::IExpression.
const mi::mdl::IExpression* int_expr_to_mdl_ast_expr(
    DB::Transaction* transaction,
    mi::mdl::IModule* module,
    const mi::mdl::IType* mdl_type,
    const IExpression* expr,
    Name_mangler* name_mangler);

/// Converts MI::MDL::IExpression to mi::mdl::DAG_node.
const mi::mdl::DAG_node* int_expr_to_mdl_dag_node(
    DB::Transaction* transaction,
    mi::mdl::IDag_builder* builder,
    const mi::mdl::IType* type,
    const IExpression* expr,
    mi::Float32 mdl_meters_per_scene_unit,
    mi::Float32 mdl_wavelength_min,
    mi::Float32 mdl_wavelength_max);

/// Converts MI::MDL::IExpression to mi::mdl::DAG_node.
const mi::mdl::DAG_node* int_expr_to_mdl_dag_node(
    DB::Transaction* transaction,
    mi::mdl::IGenerated_code_dag::DAG_node_factory* builder,
    const mi::mdl::IType* type,
    const IExpression* expr,
    mi::Float32 mdl_meters_per_scene_unit,
    mi::Float32 mdl_wavelength_min,
    mi::Float32 mdl_wavelength_max);

// ********** Misc utility functions around MI::MDL ************************************************

/// Adds a "::" prefix for builtin enum/struct symbols.
std::string prefix_symbol_name( const char* symbol);

/// Returns \c true iff the modifier-stripped types are identical, or if the modifier-stripped
/// argument type is an array and the modifier-stripped parameter type is a deferred-sized array of
/// the same element type.
/// If allow_compatible_types is true, this function also returns true, if \p argument_type can be
/// casted to \p parameter_type. In this case, the output parameter \p needs_cast is set to true.
bool argument_type_matches_parameter_type(
    IType_factory* tf,
    const IType* argument_type,
    const IType* parameter_type,
    bool allow_cast,
    bool &needs_cast);

/// Returns \c true iff the return type of the called function definition is varying. This includes
/// definitions where the \em effective return if varying, i.e., it is declared auto but the
/// function itself is varying.
///
/// \pre argument->get_kind() returns IExpression::EK_CALL
bool return_type_is_varying( DB::Transaction* transaction, const IExpression* argument);

/// Performs a deep copy of expressions.
///
/// "Deep copy" is defined as duplication of the DB elements referenced in calls and application of
/// the deep copy to its arguments. Note that referenced resources are not duplicated (to save
/// memory). Parameter references are resolved using \p context.
///
/// \param transaction            The DB transaction to use (to duplicate the attachments).
/// \param expr                   The expression from which to create the deep copy.
/// \param context                The context to resolve parameter references.
/// \return                       An copy of \p expr with all expressions replaced by duplicates.
IExpression* deep_copy(
    const IExpression_factory* ef,
    DB::Transaction* transaction,
    const IExpression* expr,
    const std::vector<mi::base::Handle<const IExpression> >& context);

/// Returns a hash value for a resource (light profiles and BSDF measurements).
///
/// Uses the MDL file path if not empty, and the tag version otherwise.
mi::Uint32 get_hash( const std::string& mdl_file_path, DB::Tag_version tv);

/// Returns a hash value for a resource (light profiles and BSDF measurements).
///
/// Uses the MDL file path if not empty, and the tag version otherwise.
mi::Uint32 get_hash( const char* mdl_file_path, DB::Tag_version tv);

/// Returns a hash value for resource (textures).
///
/// Uses the MDL file path and gamma value if the MDL file path is not empty, and the two tag
/// versions otherwise.
mi::Uint32 get_hash(
    const std::string& mdl_file_path,
    mi::Float32 gamma,
    DB::Tag_version tv1,
    DB::Tag_version tv2);

/// Returns a hash value for resource (textures).
///
/// Uses the MDL file path and gamma value if the MDL file path is not empty, and the two tag
/// versions otherwise.
mi::Uint32 get_hash(
    const char* mdl_file_path,
    mi::Float32 gamma,
    DB::Tag_version tv1,
    DB::Tag_version tv2);

/// Returns a reader for the given string.
mi::neuraylib::IReader* create_reader( const std::string& data);

/// Checks, if the material definition with the given \p definition_db_name is valid, meaning a
/// definition with that name and a matching identifier exists in its module.
/// \param transaction        transaction to use
/// \param module_tag         the tag of the module that owns the definition. Can be \c NULL for
///                           immutable calls. In that case definition_db_name is used to get the
///                           module name and access the module.
/// \param definition_id      the definition identifier.
/// \param definition_db_name the DB name of the definition.
///
/// \return \c true, if a definition with the given name and identifier still exists in the module,
///         \c false otherwise.
bool is_valid_material_definition(
    DB::Transaction* transaction,
    DB::Tag module_tag,
    Mdl_ident definition_id,
    const std::string& definition_db_name);

/// Checks, if the function definition with the given \p definition_db_name is valid, meaning a
/// definition with that name and a matching identifier exists in its module.
/// \param transaction        transaction to use
/// \param module_tag         the tag of the module that owns the definition. Can be \c NULL for
///                           immutable calls. In that case definition_db_name is used to get the
///                           module name and access the module.
/// \param definition_id      the definition identifier.
/// \param definition_db_name the DB name of the definition.
///
/// \return \c true, if a definition with the given name and identifier still exists in the module,
///         \c false otherwise.
bool is_valid_function_definition(
    DB::Transaction* transaction,
    DB::Tag module_tag,
    Mdl_ident definition_id,
    const std::string& definition_db_name);

/// Extracts the module db name from a fully qualified definition db name.
std::string get_module_db_name(const std::string& definition_db_name);

// **********  Traversal of types, values, and expressions *****************************************

/// Returns the type of a struct field identified by name (needs linear time).
///
/// \param type        An MDL struct type
/// \param field_name  The name of a field
/// \return            The MDL type of the requested field or \c NULL if the field does not exist.
const mi::mdl::IType* get_field_type( const mi::mdl::IType_struct* type, const char* field_name);

/// Looks up a sub-value according to a given path.
///
/// \param type            The MDL type corresponding to \p value.
/// \param value           The value.
/// \param path            The path to follow in \p value. The path may contain dots or bracket
///                        pairs as separators. The path component up to the next separator is used
///                        to select struct fields by name or other compound elements by index.
/// \param[out] sub_type   The MDL type corresponding to the return value, or \c NULL in case of
///                        errors.
/// \return                A sub-value for \p value according to \p path, or \c NULL in case of
///                        errors.
const IValue* lookup_sub_value(
    const mi::mdl::IType* type,
    const IValue* value,
    const char* path,
    const mi::mdl::IType** sub_type);

/// Looks up a sub value according to path.
///
/// As above, but without the type computation.
inline const IValue* lookup_sub_value( const IValue* value, const char* path)
{ return lookup_sub_value( 0, value, path, 0); }

/// Looks up a sub-expression according to path.
///
/// \param transaction     The DB transaction used to access the parameter types of direct calls.
/// \param ef              The expression factory used to wrap values as expressions.
/// \param temporaries     Used to resolve temporary expressions.
/// \param type            The MDL type corresponding to \p expr, or \c NULL. This type will be
///                        traversed in parallel with \p expr and the corresponding sub-type is
///                        returned in \p sub-type.
/// \param expr            The expression. Calls and parameter references are not supported.
///                        Temporaries are only supported if \p temporaries is not \c NULL.
/// \param path            The path to follow in \p expr. The path may contain dots or bracket pairs
///                        as separators. The path component up to the next separator is used to
///                        select struct fields or direct call arguments by name or other compound
///                        elements by index.
/// \param[out] sub_type   The MDL type corresponding to the return value, or \c NULL in case of
///                        errors.
/// \return                A sub-expression for \p expr according to \p path, or \c NULL in case of
///                        errors.
const IExpression* lookup_sub_expression(
    DB::Transaction* transaction,
    const IExpression_factory* ef,
    const IExpression_list* temporaries,
    const mi::mdl::IType* type,
    const IExpression* expr,
    const char* path,
    const mi::mdl::IType** sub_type);

/// Looks up a sub-expression according to path.
///
/// As above, but without the type computation.
inline const IExpression* lookup_sub_expression(
    const IExpression_factory* ef,
    const IExpression_list* temporaries,
    const IExpression* expr,
    const char* path)
{ return lookup_sub_expression( 0, ef, temporaries, 0, expr, path, 0); }


// ********** Misc utility functions around mi::mdl ************************************************

/// Converts deferred-sized arrays into immediate-sized arrays of length \p size.
const mi::mdl::IType_compound* convert_deferred_sized_into_immediate_sized_array(
    mi::mdl::IValue_factory* vf,
    const mi::mdl::IType_compound* mdl_type,
    mi::Size size);

/// Creates an MDL AST expression reference for a given function/material signature.
///
/// \param module                 The module on which the qualified name is created.
/// \param signature              The signature.
/// \param signature              The name mangler.
/// \return                       The MDL AST expression reference for the signature.
const mi::mdl::IExpression_reference* signature_to_reference(
    mi::mdl::IModule* module, const char* signature, Name_mangler* name_mangler);

/// Creates an MDL AST expression reference for a given MDL type.
///
/// \param module                 The module on which the qualified name is created.
/// \param type                   The type.
/// \return                       The MDL AST expression reference for the type, or for arrays the
///                               MDL AST expression reference for the corresponding array
///                               constructor.
mi::mdl::IType_name* type_to_type_name(
    mi::mdl::IModule* module, const mi::mdl::IType* type);

/// Creates an MDL AST expression reference for a given MDL type.
///
/// \param module                 The module on which the qualified name is created.
/// \param type                   The type.
/// \return                       The MDL AST expression reference for the type, or for arrays the
///                               MDL AST expression reference for the corresponding array
///                               constructor.
mi::mdl::IExpression_reference* type_to_reference(
    mi::mdl::IModule* module, const mi::mdl::IType* type);

/// Associates all resource literals inside a code DAG with their DB tags.
///
/// \param transaction            The DB transaction to use (to retrieve resource tags).
/// \param code_dag               The code DAG to update.
/// \param module_filename        The file name of the module.
/// \param module_name            The fully-qualified MDL module name.
void update_resource_literals(
    DB::Transaction* transaction,
    mi::mdl::IGenerated_code_dag* code_dag,
    const char* module_filename,
    const char* module_name);

/// Collects all resource references in all material bodies.
///
/// \param code_dag               The code DAG of the module.
/// \param[out] references        The collected references are added to this container.
void collect_resource_references(
    const mi::mdl::IGenerated_code_dag* code_dag,
    std::set<const mi::mdl::IValue_resource*>& references);

/// Collects all call references in a material body.
///
/// \param transaction            The DB transaction to use (needed to convert strings into tags).
/// \param code_dag               The code DAG of the module.
/// \param material_index         The index of the material in question.
/// \param[out] references        The collected references are added to this container.
void collect_material_references(
    DB::Transaction* transaction,
    const mi::mdl::IGenerated_code_dag* code_dag,
    mi::Uint32 material_index,
    DB::Tag_set& references);

/// Collects all call references in a function body (precomputed by MDL compiler).
///
/// \param transaction            The DB transaction to use (needed to convert strings into tags).
/// \param code_dag               The code DAG of the module.
/// \param function_index         The index of the function in question.
/// \param[out] references        The collected references are added to this container.
void collect_function_references(
    DB::Transaction* transaction,
    const mi::mdl::IGenerated_code_dag* code_dag,
    mi::Uint32 function_index,
    DB::Tag_set& references);


// ********** Symbol_importer **********************************************************************

/// A simple name mangler.
class Name_mangler
{

public:
    Name_mangler(
        mi::mdl::IMDL* imdl);

    const char* mangle(const char* sym);

    using Name_map = std::map<std::string, std::string>;

    const Name_map& get_names() { return m_unicode_to_alias; }

private:

    std::string make_unique(const std::string& ident) const;
private:

    mi::base::Handle<mi::mdl::IMDL> m_mdl;

    Name_map m_unicode_to_alias;

    std::set<std::string> m_alias;
};

class Name_importer;

/// Helper class to create imports from entities references by AST expressions.
class Symbol_importer {
public:
    typedef std::list<std::string> Name_list;
public:
    /// Constructor.
    ///
    /// \param module  The module to import into
    Symbol_importer( mi::mdl::IModule* module);

    /// Destructor.
    ~Symbol_importer();

    /// Collects all imports required by an AST expression.
    void collect_imports( const mi::mdl::IExpression* expr);

    /// Collects all imports required by a type name.
    void collect_imports(const mi::mdl::IType_name* tn);

    /// Collects all imports required by an annotation block.
    void collect_imports( const mi::mdl::IAnnotation_block* annotation_block);

    /// Add names from a list.
    void add_names( const Name_list& names);

    /// Write the collected imports into the module.
    void add_imports();

    /// Returns true if the current list of imports contains MDLE definitions.
    bool imports_mdle() const;

private:
    /// The name importer.
    Name_importer* m_name_importer;
};


// ********** Module_cache *************************************************************************

/// Adapts the DB (or rather a transaction) to the IModule_cache interface.
class Module_cache : public mi::mdl::IModule_cache
{
private:
    static std::atomic<size_t> s_context_counter;

public:
    /// Default implementation of the IMdl_loading_wait_handle interface.
    /// Used by the \c Wait_handle_factory if no other factory is registered at the module cache.
    class Wait_handle 
        : public mi::base::Interface_implement<mi::neuraylib::IMdl_loading_wait_handle>
    {
    public:
        explicit Wait_handle();

        void wait() override;
        void notify(mi::Sint32 result_code) override;
        mi::Sint32 get_result_code() const override;

    private:
        std::condition_variable m_conditional;
        std::mutex m_conditional_mutex;
        bool m_processed;
        int m_result_code;
    };

    /// Default implementation of the IMdl_loading_wait_handle_factory interface.
    /// Uses \c Wait_handles if no other factory is registered at the module cache.
    class Wait_handle_factory
        : public mi::base::Interface_implement<mi::neuraylib::IMdl_loading_wait_handle_factory>
    {
    public:
        explicit Wait_handle_factory() {}

        mi::neuraylib::IMdl_loading_wait_handle* create_wait_handle() const override;
    };

    explicit Module_cache(
        DB::Transaction* transaction,
        Mdl_module_wait_queue* queue,
        const DB::Tag_set& module_ignore_list);

    virtual ~Module_cache();

    /// If the DB contains the MDL module \p module_name, return it, otherwise \c NULL.
    /// In case of access from multiple threads, only the first thread that wants to load
    /// module will return \c NULL immediately, further threads will block until notify was called
    /// by the loading thread. In case loading failed on a different thread, \c lookup will also 
    /// return \c NULL after returning from waiting. The caller can check weather the current
    /// thread is supposed to load the module by calling \c processed_on_this_thread.
    const mi::mdl::IModule* lookup(const char* module_name) const override;

    /// Checks if the module is the DB. If so, the module is returned and NULL otherwise.
    const mi::mdl::IModule* lookup_db(const char* module_name) const;

    /// Check if this module is loading was started in the current context. I.e., on the thread 
    /// that created this module cache instance.
    /// Note, this assumes a light weight implementation of the Cache. One instance for each
    /// call to \c load module.
    ///
    /// \param module_name      The name of the module that is been processed.
    /// \return                 True if the module loading was initialized with this instance.
    bool loading_process_started_in_current_context(const char* module_name) const;

    /// Notify waiting threads about the finishing of the loading process of module.
    /// This function has to be called after a successfully loaded module was registered in the
    /// Database (or a corresponding cache structure) AND also in case of loading failures.
    ///
    /// \param module_name      The name of the module that has been processed.
    /// \param result_code      Return code of the loading process. 0 in case of success.
    virtual void notify(const char* module_name, int result_code);

    /// Get the module loading callback
    mi::mdl::IModule_loaded_callback* get_module_loading_callback() const override;

    /// Set the module loading callback.
    void set_module_loading_callback(mi::mdl::IModule_loaded_callback* callback);

    /// Get the module cache wait handle factory.
    const mi::neuraylib::IMdl_loading_wait_handle_factory* get_wait_handle_factory() const;

    /// Set the module cache wait handle factory.
    void set_wait_handle_factory(mi::neuraylib::IMdl_loading_wait_handle_factory* factory);

    /// Get an unique identifier for the context in which the current module is loaded.
    ///
    /// \return                 The identifier.
    size_t get_loading_context_id() const { return m_context_id; }

private:

    size_t m_context_id;
    DB::Transaction* m_transaction;
    Mdl_module_wait_queue* m_queue;
    mi::mdl::IModule_loaded_callback* m_module_load_callback;
    mi::base::Handle<mi::neuraylib::IMdl_loading_wait_handle_factory> m_default_wait_handle_factory;
    mi::base::Handle<mi::neuraylib::IMdl_loading_wait_handle_factory> m_user_wait_handle_factory;
    DB::Tag_set m_ignore_list;
};

// ********** Call_evaluator ***********************************************************************

/// Evaluates calls during material compilation.
///
/// Used to fold resource-related calls into constants.
class Call_evaluator : public mi::mdl::ICall_evaluator
{
public:
    Call_evaluator(
        DB::Transaction* transaction,
        bool has_resource_attributes)
    : m_transaction(transaction)
    , m_has_resource_attributes(has_resource_attributes)
    {}

    virtual ~Call_evaluator() {}

    /// Check whether evaluate_intrinsic_function() should be called for an unhandled
    /// intrinsic functions with the given semantic.
    ///
    /// \param semantic  the semantic to check for
    bool is_evaluate_intrinsic_function_enabled(
        mi::mdl::IDefinition::Semantics semantic) const;

    /// Called by IExpression_call::fold() to evaluate unhandled intrinsic functions.
    ///
    /// \param semantic     the semantic of the function to call
    /// \param arguments    the arguments for the call
    /// \param n_arguments  the number of arguments
    ///
    /// \return IValue_bad if this function could not be evaluated, its value otherwise
    const mi::mdl::IValue* evaluate_intrinsic_function(
        mi::mdl::IValue_factory* value_factory,
        mi::mdl::IDefinition::Semantics semantic,
        const mi::mdl::IValue* const arguments[],
        size_t n_arguments) const;

private:
    /// Folds df::light_profile_power() to a constant, or returns IValue_bad in case of errors.
    const mi::mdl::IValue* fold_df_light_profile_power(
        mi::mdl::IValue_factory* value_factory, const mi::mdl::IValue* argument) const;

    /// Folds df::light_profile_maximum() to a constant, or returns IValue_bad in case of errors.
    const mi::mdl::IValue* fold_df_light_profile_maximum(
        mi::mdl::IValue_factory* value_factory, const mi::mdl::IValue* argument) const;

    /// Folds df::light_profile_isinvalid() to a constant, or returns IValue_bad in case of errors.
    const mi::mdl::IValue* fold_df_light_profile_isvalid(
        mi::mdl::IValue_factory* value_factory, const mi::mdl::IValue* argument) const;

    /// Folds df::bsdf_measurement_isvalid() to a constant, or returns IValue_bad in case of errors.
    const mi::mdl::IValue* fold_df_bsdf_measurement_isvalid(
        mi::mdl::IValue_factory* value_factory, const mi::mdl::IValue* argument) const;

    /// Folds tex::width() to a constant, or returns IValue_bad in case of errors.
    /// uvtile_arg may be NULL for non-uvtile texture calls.
    const mi::mdl::IValue* fold_tex_width(
        mi::mdl::IValue_factory* value_factory,
        const mi::mdl::IValue* argument,
        const mi::mdl::IValue* uvtile_arg) const;

    /// Folds tex::height() to a constant, or returns IValue_bad in case of errors.
    /// uvtile_arg may be NULL for non-uvtile texture calls.
    const mi::mdl::IValue* fold_tex_height(
        mi::mdl::IValue_factory* value_factory,
        const mi::mdl::IValue* argument,
        const mi::mdl::IValue* uvtile_arg) const;

    /// Folds tex::depth() to a constant, or returns IValue_bad in case of errors.
    const mi::mdl::IValue* fold_tex_depth(
        mi::mdl::IValue_factory* value_factory, const mi::mdl::IValue* argument) const;

    /// Folds tex::texture_invalid() to a constant, or returns IValue_bad in case of errors.
    const mi::mdl::IValue* fold_tex_texture_isvalid(
        mi::mdl::IValue_factory* value_factory, const mi::mdl::IValue* argument) const;

    DB::Transaction* m_transaction;
    bool m_has_resource_attributes;
};


// ********** Mdl_material_instance_builder ********************************************************

class Mdl_material_instance_builder
{
public:
    /// Creates an MDL material instance from a compiled material.
    ///
    /// \param transaction    The DB transaction to use.
    /// \param material       The compiled material to convert.
    /// \return               The created MDL material instance for \p material, or \c NULL in case
    ///                       of errors.
    mi::mdl::IGenerated_code_dag::IMaterial_instance* create_material_instance(
        DB::Transaction* transaction, const Mdl_compiled_material* material);
};

/// Check if it is possible to enforce the uniform property if the new parameter is uniform
///
/// \param[in]  transaction      current transaction
/// \param[in]  args             argument list
/// \param[in]  param_types      parameter type list
/// \param[in]  path             path to the newly parameter
/// \param[in]  expr             the expression that will be turned into parameter
/// \param[out] must_be_uniform  \c true, iff the newly created parameter must be uniform to
///                              enforce the uniform property
///
/// \return true if possible, false if not possible
bool can_enforce_uniform(
    DB::Transaction* transaction,
    mi::base::Handle<const IExpression_list> const &args,
    mi::base::Handle<const IType_list> const &param_types,
    std::string const &path,
    mi::base::Handle<const IExpression> const &expr,
    bool &must_be_uniform);

/// Find the expression a path is pointing on.
///
/// \param transaction  the current transaction
/// \param path         the path
/// \param args         the material arguments
mi::base::Handle<const IExpression> find_path(
    DB::Transaction* transaction,
    const std::string& path,
    const mi::base::Handle<const IExpression_list>& args);

/// Write an UUID to the given serializer.
///
/// \param serializer  the serializer to be used
/// \param uuid        the UUID to be written
void write(SERIAL::Serializer* serializer, const mi::base::Uuid& uuid);

/// Read an UUID from the given deserializer.
///
/// \param deserializer  the deserializer to be used
/// \param uuid          the UUID to be read
void read(SERIAL::Deserializer* deserializer, mi::base::Uuid& uuid);

/// Converts a hash from the MDL API representation to the base API representation.
mi::base::Uuid convert_hash( mi::mdl::DAG_hash const &hash);

/// Generates a unique ID.
Uint64 generate_unique_id();

/// Repairs the given argument list according to the given rules.
/// \param transaction  the transaction.
/// \param arguments    the arguments to repair.
/// \param defaults     argument defaults.
/// \param repair_calls if \c true, invalid calls will be repaired, if possible.
/// \param remove_calls if \c true, invalid calls will be replaced by a default. In case \p
///                     repair_calls is \c true, the call will be removed if the repair
///                     failed.
/// \param level        recursion level
/// \param context      execution context to propagate error messages.
/// \return 
///      -  0:   Success
///      - -1:   Repairing failed. See the context for details.
mi::Sint32 repair_arguments(
    DB::Transaction* transaction,
    IExpression_list* arguments,
    const IExpression_list* defaults,
    bool repair_calls,
    bool remove_calls,
    mi::Uint32 level,
    Execution_context* context);

} // namespace MDL

} // namespace MI

#endif // IO_SCENE_MDL_ELEMENTS_MDL_ELEMENTS_UTILITIES_H
