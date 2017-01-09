"use strict";

let {FormAutofillContent} = loadFormAutofillContent();

const TESTCASES = [
  {
    description: "Form containing 5 fields with autocomplete attribute.",
    document: `<form>
                 <input id="street-addr" autocomplete="street-address">
                 <input id="city" autocomplete="address-level2">
                 <input id="country" autocomplete="country">
                 <input id="email" autocomplete="email">
                 <input id="tel" autocomplete="tel">
                 <input id="without-autocomplete-1">
                 <input id="without-autocomplete-2">
               </form>`,
    expectedResult: [
      "street-addr",
      "city",
      "country",
      "email",
      "tel",
    ],
  },
  {
    description: "Form containing only 2 fields with autocomplete attribute.",
    document: `<form>
                 <input id="street-addr" autocomplete="street-address">
                 <input id="city" autocomplete="address-level2">
                 <input id="without-autocomplete-1">
                 <input id="without-autocomplete-2">
               </form>`,
    expectedResult: [],
  },
  {
    description: "Fields without form element.",
    document: `<input id="street-addr" autocomplete="street-address">
               <input id="city" autocomplete="address-level2">
               <input id="country" autocomplete="country">
               <input id="email" autocomplete="email">
               <input id="tel" autocomplete="tel">
               <input id="without-autocomplete-1">
               <input id="without-autocomplete-2">`,
    expectedResult: [
      "street-addr",
      "city",
      "country",
      "email",
      "tel",
    ],
  },
];

let markedFieldId = [];
FormAutofillContent._markAsAutofillField = function(field) {
  markedFieldId.push(field.id);
};

TESTCASES.forEach(testcase => {
  add_task(function* () {
    do_print("Starting testcase: " + testcase.description);

    markedFieldId = [];

    let doc = MockDocument.createTestDocument(
      "http://localhost:8080/test/", testcase.document);
    FormAutofillContent._identifyAutofillFields(doc);

    Assert.deepEqual(markedFieldId, testcase.expectedResult,
      "Check the fields were marked correctly.");
  });
});
