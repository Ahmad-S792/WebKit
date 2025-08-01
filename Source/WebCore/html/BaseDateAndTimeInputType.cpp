/*
 * Copyright (C) 2010-2013 Google Inc. All rights reserved.
 * Copyright (C) 2016-2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "BaseDateAndTimeInputType.h"

#include "AXObjectCache.h"
#include "BaseClickableWithKeyInputType.h"
#include "Chrome.h"
#include "ContainerNodeInlines.h"
#include "DateComponents.h"
#include "DateTimeChooserParameters.h"
#include "Decimal.h"
#include "FocusController.h"
#include "HTMLDataListElement.h"
#include "HTMLDivElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLOptionElement.h"
#include "KeyboardEvent.h"
#include "LocalFrameView.h"
#include "NodeName.h"
#include "Page.h"
#include "PlatformLocale.h"
#include "RenderElement.h"
#include "ScriptDisallowedScope.h"
#include "Settings.h"
#include "ShadowRoot.h"
#include "StepRange.h"
#include "Text.h"
#include "TypedElementDescendantIteratorInlines.h"
#include "UserAgentParts.h"
#include "UserGestureIndicator.h"
#include <limits>
#include <wtf/DateMath.h>
#include <wtf/MathExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/StringView.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(BaseDateAndTimeInputType);

using namespace HTMLNames;

static const int msecPerMinute = 60 * 1000;
static const int msecPerSecond = 1000;

void BaseDateAndTimeInputType::DateTimeFormatValidator::visitField(DateTimeFormat::FieldType fieldType, int)
{
    switch (fieldType) {
    case DateTimeFormat::FieldTypeYear:
        m_results.add(DateTimeFormatValidationResults::HasYear);
        break;

    case DateTimeFormat::FieldTypeMonth:
    case DateTimeFormat::FieldTypeMonthStandAlone:
        m_results.add(DateTimeFormatValidationResults::HasMonth);
        break;

    case DateTimeFormat::FieldTypeWeekOfYear:
        m_results.add(DateTimeFormatValidationResults::HasWeek);
        break;

    case DateTimeFormat::FieldTypeDayOfMonth:
        m_results.add(DateTimeFormatValidationResults::HasDay);
        break;

    case DateTimeFormat::FieldTypePeriod:
        m_results.add(DateTimeFormatValidationResults::HasMeridiem);
        break;

    case DateTimeFormat::FieldTypeHour11:
    case DateTimeFormat::FieldTypeHour12:
        m_results.add(DateTimeFormatValidationResults::HasHour);
        break;

    case DateTimeFormat::FieldTypeHour23:
    case DateTimeFormat::FieldTypeHour24:
        m_results.add(DateTimeFormatValidationResults::HasHour);
        m_results.add(DateTimeFormatValidationResults::HasMeridiem);
        break;

    case DateTimeFormat::FieldTypeMinute:
        m_results.add(DateTimeFormatValidationResults::HasMinute);
        break;

    case DateTimeFormat::FieldTypeSecond:
        m_results.add(DateTimeFormatValidationResults::HasSecond);
        break;

    default:
        break;
    }
}

bool BaseDateAndTimeInputType::DateTimeFormatValidator::validateFormat(const String& format, const BaseDateAndTimeInputType& inputType)
{
    if (!DateTimeFormat::parse(format, *this))
        return false;
    return inputType.isValidFormat(m_results);
}

BaseDateAndTimeInputType::~BaseDateAndTimeInputType()
{
    closeDateTimeChooser();
}

WallTime BaseDateAndTimeInputType::valueAsDate() const
{
    return WallTime::fromRawSeconds(Seconds::fromMilliseconds(valueAsDouble()).value());
}

ExceptionOr<void> BaseDateAndTimeInputType::setValueAsDate(WallTime value) const
{
    ASSERT(element());
    protectedElement()->setValue(serializeWithMilliseconds(value.secondsSinceEpoch().milliseconds()));
    return { };
}

WallTime BaseDateAndTimeInputType::accessibilityValueAsDate() const
{
    double dateAsDouble = valueAsDouble();
    if (std::isnan(dateAsDouble) && m_dateTimeEditElement) {
        // The value for this element has not been set. Try to get a value from
        // m_dateTimeEditElement if exists. That value may have been indirectly
        // set as placeholder values for the field elements.
        Ref dateTimeEditElement = *m_dateTimeEditElement;
        String value = dateTimeEditElement->value();
        if (value.isEmpty())
            value = dateTimeEditElement->placeholderValue();
        if (value.isEmpty())
            return { };

        auto decimal = parseToNumber(value, Decimal::nan());
        if (decimal.isFinite())
            dateAsDouble = decimal.toDouble();
    }

    if (std::isnan(dateAsDouble))
        return { };
    return WallTime::fromRawSeconds(Seconds::fromMilliseconds(dateAsDouble).value());
}

double BaseDateAndTimeInputType::valueAsDouble() const
{
    ASSERT(element());
    const Decimal value = parseToNumber(protectedElement()->value(), Decimal::nan());
    return value.isFinite() ? value.toDouble() : DateComponents::invalidMilliseconds();
}

ExceptionOr<void> BaseDateAndTimeInputType::setValueAsDecimal(const Decimal& newValue, TextFieldEventBehavior eventBehavior) const
{
    ASSERT(element());
    protectedElement()->setValue(serialize(newValue), eventBehavior);
    return { };
}

bool BaseDateAndTimeInputType::typeMismatchFor(const String& value) const
{
    return !value.isEmpty() && !parseToDateComponents(value);
}

bool BaseDateAndTimeInputType::typeMismatch() const
{
    ASSERT(element());
    return typeMismatchFor(protectedElement()->value());
}

bool BaseDateAndTimeInputType::hasBadInput() const
{
    ASSERT(element());
    return protectedElement()->value()->isEmpty() && m_dateTimeEditElement && protectedDateTimeEditElement()->editableFieldsHaveValues();
}

Decimal BaseDateAndTimeInputType::defaultValueForStepUp() const
{
    double ms = WallTime::now().secondsSinceEpoch().milliseconds();
    int offset = calculateLocalTimeOffset(ms).offset / msPerMinute;
    return Decimal::fromDouble(ms + (offset * msPerMinute));
}

Decimal BaseDateAndTimeInputType::parseToNumber(const String& source, const Decimal& defaultValue) const
{
    auto date = parseToDateComponents(source);
    if (!date)
        return defaultValue;
    double msec = date->millisecondsSinceEpoch();
    ASSERT(std::isfinite(msec));
    return Decimal::fromDouble(msec);
}

String BaseDateAndTimeInputType::serialize(const Decimal& value) const
{
    if (!value.isFinite())
        return { };
    auto date = setMillisecondToDateComponents(value.toDouble());
    if (!date)
        return { };
    return serializeWithComponents(*date);
}

String BaseDateAndTimeInputType::serializeWithComponents(const DateComponents& date) const
{
    ASSERT(element());
    Decimal step;
    if (!protectedElement()->getAllowedValueStep(&step) || step.remainder(msecPerMinute).isZero())
        return date.toString();
    if (step.remainder(msecPerSecond).isZero())
        return date.toString(SecondFormat::Second);
    return date.toString(SecondFormat::Millisecond);
}

String BaseDateAndTimeInputType::serializeWithMilliseconds(double value) const
{
    return serialize(Decimal::fromDouble(value));
}

String BaseDateAndTimeInputType::localizeValue(const String& proposedValue) const
{
    auto date = parseToDateComponents(proposedValue);
    if (!date)
        return proposedValue;

    ASSERT(element());
    String localized = protectedElement()->locale().formatDateTime(*date);
    return localized.isEmpty() ? proposedValue : localized;
}

String BaseDateAndTimeInputType::visibleValue() const
{
    ASSERT(element());
    return localizeValue(protectedElement()->value());
}

ValueOrReference<String> BaseDateAndTimeInputType::sanitizeValue(const String& proposedValue LIFETIME_BOUND) const
{
    if (typeMismatchFor(proposedValue))
        return emptyString();
    return proposedValue;
}

bool BaseDateAndTimeInputType::supportsReadOnly() const
{
    return true;
}

bool BaseDateAndTimeInputType::shouldRespectListAttribute()
{
    return false;
}

bool BaseDateAndTimeInputType::valueMissing(const String& value) const
{
    ASSERT(element());
    return protectedElement()->isMutable() && element()->isRequired() && value.isEmpty();
}

bool BaseDateAndTimeInputType::isKeyboardFocusable(const FocusEventData&) const
{
    ASSERT(element());
    Ref input = *element();
    return !input->isReadOnly() && input->isTextFormControlFocusable();
}

bool BaseDateAndTimeInputType::isMouseFocusable() const
{
    ASSERT(element());
    return protectedElement()->isTextFormControlFocusable();
}

bool BaseDateAndTimeInputType::shouldHaveSecondField(const DateComponents& date) const
{
    if (date.second())
        return true;

    auto stepRange = createStepRange(AnyStepHandling::Default);
    return !stepRange.minimum().remainder(msecPerMinute).isZero()
        || !stepRange.step().remainder(msecPerMinute).isZero();
}

bool BaseDateAndTimeInputType::shouldHaveMillisecondField(const DateComponents& date) const
{
    if (date.millisecond())
        return true;

    auto stepRange = createStepRange(AnyStepHandling::Default);
    return !stepRange.minimum().remainder(msecPerSecond).isZero()
        || !stepRange.step().remainder(msecPerSecond).isZero();
}

void BaseDateAndTimeInputType::setValue(const String& value, bool valueChanged, TextFieldEventBehavior eventBehavior, TextControlSetValueSelection selection)
{
    InputType::setValue(value, valueChanged, eventBehavior, selection);
    if (valueChanged)
        updateInnerTextValue();
}

void BaseDateAndTimeInputType::handleDOMActivateEvent(Event&)
{
    ASSERT(element());
    if (!element()->renderer() || !protectedElement()->isMutable() || !UserGestureIndicator::processingUserGesture())
        return;

    if (m_dateTimeChooser)
        return;

    showPicker();
}

void BaseDateAndTimeInputType::showPicker()
{
    if (!element()->renderer())
        return;

    if (!element()->document().page())
        return;

    DateTimeChooserParameters parameters;
    if (!setupDateTimeChooserParameters(parameters))
        return;

    if (auto* chrome = this->chrome()) {
        m_dateTimeChooser = chrome->createDateTimeChooser(*this);
        if (RefPtr dateTimeChooser = m_dateTimeChooser)
            dateTimeChooser->showChooser(parameters);
    }
}

void BaseDateAndTimeInputType::createShadowSubtree()
{
    ASSERT(needsShadowSubtree());
    ASSERT(element());

    Ref element = *this->element();
    Ref document = element->document();

    Ref shadowRoot = *element->userAgentShadowRoot();
    ScriptDisallowedScope::EventAllowedScope eventAllowedScope { shadowRoot };

    if (document->settings().dateTimeInputsEditableComponentsEnabled()) {
        Ref dateTimeEditElement = DateTimeEditElement::create(document, *this);
        m_dateTimeEditElement = dateTimeEditElement.copyRef();
        shadowRoot->appendChild(ContainerNode::ChildChange::Source::Parser, dateTimeEditElement);
    } else {
        Ref valueContainer = HTMLDivElement::create(document);
        shadowRoot->appendChild(ContainerNode::ChildChange::Source::Parser, valueContainer);
        valueContainer->setUserAgentPart(UserAgentParts::webkitDateAndTimeValue());
    }
    updateInnerTextValue();
}

void BaseDateAndTimeInputType::removeShadowSubtree()
{
    InputType::removeShadowSubtree();
    m_dateTimeEditElement = nullptr;
}

void BaseDateAndTimeInputType::updateInnerTextValue()
{
    ASSERT(element());

    createShadowSubtreeIfNeeded();

    Ref input = *element();

    if (!m_dateTimeEditElement) {
        RefPtr firstChildElement = dynamicDowncast<HTMLElement>(input->userAgentShadowRoot()->firstChild());
        if (!firstChildElement)
            return;
        auto displayValue = visibleValue();
        if (displayValue.isEmpty()) {
            // Need to put something to keep text baseline.
            displayValue = " "_s;
        }
        firstChildElement->setInnerText(WTFMove(displayValue));
        return;
    }

    DateTimeEditElement::LayoutParameters layoutParameters(input->locale());

    auto date = parseToDateComponents(input->value().get());
    if (date)
        setupLayoutParameters(layoutParameters, *date);
    else {
        if (auto dateForLayout = setMillisecondToDateComponents(createStepRange(AnyStepHandling::Default).minimum().toDouble()))
            setupLayoutParameters(layoutParameters, *dateForLayout);
        else
            setupLayoutParameters(layoutParameters, DateComponents());
    }

    if (!DateTimeFormatValidator().validateFormat(layoutParameters.dateTimeFormat, *this))
        layoutParameters.dateTimeFormat = layoutParameters.fallbackDateTimeFormat;

    if (date)
        protectedDateTimeEditElement()->setValueAsDate(layoutParameters, *date);
    else
        protectedDateTimeEditElement()->setEmptyValue(layoutParameters);
}

bool BaseDateAndTimeInputType::hasCustomFocusLogic() const
{
    if (m_dateTimeEditElement)
        return false;
    return InputType::hasCustomFocusLogic();
}

void BaseDateAndTimeInputType::attributeChanged(const QualifiedName& name)
{
    switch (name.nodeName()) {
    case AttributeNames::maxAttr:
    case AttributeNames::minAttr:
        if (RefPtr element = this->element())
            element->invalidateStyleForSubtree();
        break;
    case AttributeNames::valueAttr:
        if (RefPtr element = this->element()) {
            if (!element->hasDirtyValue())
                updateInnerTextValue();
        }
        break;
    case AttributeNames::stepAttr:
        if (m_dateTimeEditElement)
            updateInnerTextValue();
        break;
    default:
        break;
    }

    InputType::attributeChanged(name);
}

void BaseDateAndTimeInputType::elementDidBlur()
{
    if (!m_dateTimeEditElement)
        closeDateTimeChooser();
}

void BaseDateAndTimeInputType::detach()
{
    closeDateTimeChooser();
}

bool BaseDateAndTimeInputType::isPresentingAttachedView() const
{
    return !!m_dateTimeChooser;
}

auto BaseDateAndTimeInputType::handleKeydownEvent(KeyboardEvent& event) -> ShouldCallBaseEventHandler
{
    ASSERT(element());
    return BaseClickableWithKeyInputType::handleKeydownEvent(*protectedElement(), event);
}

void BaseDateAndTimeInputType::handleKeypressEvent(KeyboardEvent& event)
{
    // The return key should not activate the element, as it conflicts with
    // the key binding to submit a form.
    if (event.charCode() == '\r')
        return;

    ASSERT(element());
    BaseClickableWithKeyInputType::handleKeypressEvent(*protectedElement(), event);
}

void BaseDateAndTimeInputType::handleKeyupEvent(KeyboardEvent& event)
{
    BaseClickableWithKeyInputType::handleKeyupEvent(*this, event);
}

void BaseDateAndTimeInputType::handleFocusEvent(Node* oldFocusedNode, FocusDirection direction)
{
    if (!m_dateTimeEditElement) {
        InputType::handleFocusEvent(oldFocusedNode, direction);
        return;
    }

    // If the element contains editable components, the element itself should not
    // be focused. Instead, one of it's children should receive focus.

    if (direction == FocusDirection::Backward) {
        // If the element received focus when going backwards, advance the focus one more time
        // so that this element no longer has focus. In this case, one of the children should
        // not be focused as the element is losing focus entirely.
        if (RefPtr page = element()->document().page())
            page->focusController().advanceFocus(direction, 0);

    } else {
        // If the element received focus in any other direction, transfer focus to the first focusable child.
        protectedDateTimeEditElement()->focusByOwner();
    }
}

bool BaseDateAndTimeInputType::accessKeyAction(bool sendMouseEvents)
{
    InputType::accessKeyAction(sendMouseEvents);
    ASSERT(element());
    return BaseClickableWithKeyInputType::accessKeyAction(*protectedElement(), sendMouseEvents);
}

void BaseDateAndTimeInputType::didBlurFromControl()
{
    closeDateTimeChooser();

    RefPtr element = this->element();
    if (element && element->wasChangedSinceLastFormControlChangeEvent())
        element->dispatchFormControlChangeEvent();
}

void BaseDateAndTimeInputType::didChangeValueFromControl()
{
    Ref input = *element();

    String value = sanitizeValue(protectedDateTimeEditElement()->value());
    bool valueChanged = !equalIgnoringNullity(value, input->value());

    InputType::setValue(value, valueChanged, DispatchNoEvent, DoNotSet);

    if (!valueChanged) {
        if (CheckedPtr cache = input->protectedDocument()->existingAXObjectCache()) {
            // This method is called when a sub-field of a date or time input changes. An HTML input's DOM value
            // only changes when all fields are filled out, but accessibility needs to represent the partial value
            // for assistive technologies, so notify accessibility here so it can take the appropriate actions, e.g.
            // updating the accessibility tree.
            cache->valueChanged(input.get());
        }
        return;
    }

    if (input->protectedUserAgentShadowRoot()->containsFocusedElement())
        input->dispatchFormControlInputEvent();
    else
        input->dispatchFormControlChangeEvent();

    DateTimeChooserParameters parameters;
    if (!setupDateTimeChooserParameters(parameters))
        return;

    if (RefPtr dateTimeChooser = m_dateTimeChooser)
        dateTimeChooser->showChooser(parameters);
}

bool BaseDateAndTimeInputType::isEditControlOwnerDisabled() const
{
    ASSERT(element());
    return element()->isDisabledFormControl();
}

bool BaseDateAndTimeInputType::isEditControlOwnerReadOnly() const
{
    ASSERT(element());
    return protectedElement()->isReadOnly();
}

AtomString BaseDateAndTimeInputType::localeIdentifier() const
{
    ASSERT(element());
    return protectedElement()->effectiveLang();
}

void BaseDateAndTimeInputType::didChooseValue(StringView value)
{
    ASSERT(element());
    protectedElement()->setValue(value.toString(), DispatchInputAndChangeEvent);
}

bool BaseDateAndTimeInputType::setupDateTimeChooserParameters(DateTimeChooserParameters& parameters)
{
    ASSERT(element());

    Ref element = *this->element();
    Ref document = element->document();

    if (!document->view())
        return false;

    parameters.type = element->type();
    parameters.minimum = element->minimum();
    parameters.maximum = element->maximum();
    parameters.required = element->isRequired();

    if (!document->settings().langAttributeAwareFormControlUIEnabled())
        parameters.locale = AtomString { defaultLanguage() };
    else {
        AtomString computedLocale = element->effectiveLang();
        parameters.locale = computedLocale.isEmpty() ? AtomString(defaultLanguage()) : computedLocale;
    }

    auto stepRange = createStepRange(AnyStepHandling::Reject);
    if (stepRange.hasStep()) {
        parameters.step = stepRange.step().toDouble();
        parameters.stepBase = stepRange.stepBase().toDouble();
    } else {
        parameters.step = 1.0;
        parameters.stepBase = 0;
    }

    if (CheckedPtr renderer = element->renderer())
        parameters.anchorRectInRootView = document->protectedView()->contentsToRootView(renderer->absoluteBoundingBoxRect());
    else
        parameters.anchorRectInRootView = IntRect();
    parameters.currentValue = element->value();

    CheckedRef computedStyle = *element->computedStyle();
    parameters.isAnchorElementRTL = computedStyle->writingMode().computedTextDirection() == TextDirection::RTL;
    parameters.useDarkAppearance = document->useDarkAppearance(computedStyle.ptr());
    auto date = valueOrDefault(parseToDateComponents(element->value().get()));
    parameters.hasSecondField = shouldHaveSecondField(date);
    parameters.hasMillisecondField = shouldHaveMillisecondField(date);

    if (auto dataList = element->dataList()) {
        for (Ref option : dataList->suggestions()) {
            auto label = option->label();
            auto value = option->value();
            if (!element->isValidValue(value))
                continue;
            parameters.suggestionValues.append(element->sanitizeValue(value));
            parameters.localizedSuggestionValues.append(element->localizeValue(value));
            parameters.suggestionLabels.append(value == label ? String() : label);
        }
    }

    return true;
}

void BaseDateAndTimeInputType::closeDateTimeChooser()
{
    if (RefPtr dateTimeChooser = m_dateTimeChooser)
        dateTimeChooser->endChooser();
}

} // namespace WebCore
