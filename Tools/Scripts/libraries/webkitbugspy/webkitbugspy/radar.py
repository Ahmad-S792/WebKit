# Copyright (C) 2022-2023 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import calendar
import os
import re
import subprocess
import sys
import time
import webkitcorepy

from webkitcorepy import Environment, decorators
from webkitbugspy import Issue, Tracker as GenericTracker, User, name as library_name, version as library_version


class Priority(object):
    SHOW_STOPPER = 1
    EXPECTED = 2
    IMPORTANT = 3
    NICE_TO_HAVE = 4
    NOT_SET = 5


class Tracker(GenericTracker):
    RES = [
        re.compile(r'<?rdar://problem/(?P<id>\d+)>?'),
        re.compile(r'<?radar://problem/(?P<id>\d+)>?'),
        re.compile(r'<?rdar:\/\/(?P<id>\d+)>?'),
        re.compile(r'<?radar:\/\/(?P<id>\d+)>?'),
        re.compile(r'<?https:\/\/rdar\.apple\.com\/(?P<id>\d+)>?'),
    ]

    OTHER_BUG = 'Other Bug'
    SECURITY = 'Security'
    CRASH_HANG_DATA_LOSS = 'Crash/Hang/Data Loss'
    POWER = 'Power'
    PERFORMANCE = 'Performance'
    UI_USABILITY = 'UI/Usability'
    SERIOUS_BUG = 'Serious Bug'
    FEATURE = 'Feature (New)'
    ENHANCEMENT = 'Enhancement'
    TASK = 'Task'

    CLASSIFICATIONS = [
        OTHER_BUG,
        SECURITY,
        CRASH_HANG_DATA_LOSS,
        POWER,
        PERFORMANCE,
        UI_USABILITY,
        SERIOUS_BUG,
        FEATURE,
        ENHANCEMENT,
        TASK
    ]

    RELATIONSHIP_TYPES = ['related-to', 'blocked-by', 'blocking', 'parent-of', 'subtask-of', 'cause-of', 'caused-by', 'duplicate-of', 'original-of', 'clone-of', 'cloned-to']

    ALWAYS = 'Always'
    SOMETIMES = 'Sometimes'
    RARELY = 'Rarely'
    UNABLE = 'Unable'
    DIDNT_TRY = "I Didn't Try"
    NOT_APPLICABLE = 'Not Applicable'

    REPRODUCIBILITY = [NOT_APPLICABLE, ALWAYS, SOMETIMES, RARELY, UNABLE, DIDNT_TRY]
    NAME = 'Radar'

    class Encoder(GenericTracker.Encoder):
        @decorators.hybridmethod
        def default(context, obj):
            if isinstance(obj, Tracker):
                return dict(
                    type='radar',
                    projects=obj._projects,
                    hide_title=obj.hide_title,
                )
            if isinstance(context, type):
                raise TypeError('Cannot invoke parent class when classmethod')
            return super(Tracker.Encoder, context).default(obj)


    @staticmethod
    def radarclient():
        try:
            import radarclient
            return radarclient
        except ImportError:
            return None

    def handle_access_exception(func):
        def try_func(self, *args, **kwargs):
            try:
                return func(self, *args, **kwargs)
            except self.radarclient().exceptions.RadarAccessDeniedResponseException as e:
                sys.stderr.write(f'{e.code} Permission Denied\n')
                sys.stderr.write(f'{e.reason}\n')
            except self.radarclient().exceptions.UnsuccessfulResponseException as e:
                sys.stderr.write(f'{e.reason}\n')
                sys.exit(1)
        return try_func

    def __init__(self, users=None, authentication=None, project=None, projects=None, redact=None, hide_title=None, redact_exemption=None):
        hide_title = True if hide_title is None else hide_title
        super(Tracker, self).__init__(users=users, redact=redact, redact_exemption=redact_exemption, hide_title=hide_title)
        self._projects = [project] if project else (projects or [])

        self._keywords = dict()
        self._invalid_keywords = set()

        self.library = self.radarclient()
        authentication = authentication or (self.authentication() if self.library else None)
        if authentication:
            self.client = self.library.RadarClient(
                authentication, self.library.ClientSystemIdentifier(library_name, str(library_version)),
                retry_policy=self.radarclient().RetryPolicy(),
            )
        else:
            self.client = None

    def authentication(self):
        identity = Environment.instance().get('RADAR_IDENTITY')
        username = Environment.instance().get('RADAR_USERNAME')
        password = Environment.instance().get('RADAR_PASSWORD')
        totp_secret = Environment.instance().get('RADAR_TOTP_SECRET')
        totp_id = Environment.instance().get('RADAR_TOTP_ID') or 1

        try:
            if identity and os.path.isdir(identity):
                return self.library.AuthenticationStrategyNarrative(identity)
            if username and password and totp_secret and totp_id:
                return self.library.AuthenticationStrategySystemAccountOAuth(
                    username, password, totp_secret, totp_id,
                )
            return self.library.AuthenticationStrategyAppleConnect()
        except Exception:
            sys.stderr.write('No valid authentication session for Radar\n')
            return None

    def from_string(self, string):
        for regex in self.RES:
            match = regex.match(string)
            if match:
                return self.issue(int(match.group('id')))
        return None

    def user(self, name=None, username=None, email=None):
        user = super(Tracker, self).user(name=name, username=username, email=email)
        if user:
            return user
        if not name or not username or not email:
            found = None
            try:
                if isinstance(username, int):
                    found = self.library.AppleDirectoryQuery.user_entry_for_dsid(int(username))
                elif username:
                    found = self.library.AppleDirectoryQuery.user_entry_for_attribute_value('uid', '{}@APPLECONNECT.APPLE.COM'.format(username))
                elif email:
                    found = self.library.AppleDirectoryQuery.user_entry_for_attribute_value('mail', email)
                elif name:
                    found = self.library.AppleDirectoryQuery.user_entry_for_attribute_value('cn', name)
            except subprocess.CalledProcessError:
                pass
            if not found:
                return self.users.create(
                    name=name or username,
                    username=None,
                    emails=[email],
                )
            name = '{} {}'.format(found.first_name(), found.last_name())
            username = found.dsid()
            email = found.email()
        return self.users.create(
            name=name,
            username=username,
            emails=[email],
        )

    @decorators.Memoize()
    def me(self):
        if self.client:
            user = self.client.current_user()
            if user:
                return self.users.create(
                    name='{} {}'.format(user.firstName, user.lastName),
                    username=user.dsid,
                    emails=[user.email],
                )
        return None

    def issue(self, id):
        return Issue(id=int(id), tracker=self)

    @handle_access_exception
    def populate(self, issue, member=None):
        issue._link = 'rdar://{}'.format(issue.id)
        issue._labels = []
        issue._related_links = []  # We don't yet have a defined idiom for "related links" in radar
        if (not self.client or not self.library) and member:
            sys.stderr.write('radarclient inaccessible on this machine\n')
            return issue

        if not member or member == 'labels':
            return issue

        additional_fields = []
        if member == 'source_changes':
            additional_fields.append('sourceChanges')
        radar = self.client.radar_for_id(issue.id, additional_fields=additional_fields)
        if not radar:
            sys.stderr.write("Failed to fetch '{}'\n".format(issue.link))
            return issue

        issue._title = radar.title
        issue._timestamp = int(calendar.timegm(radar.createdAt.timetuple()))
        issue._modified = int(calendar.timegm(radar.lastModifiedAt.timetuple()))
        issue._assignee = self.user(
            name='{} {}'.format(radar.assignee.firstName, radar.assignee.lastName),
            username=radar.assignee.dsid,
            email=radar.assignee.email,
        )
        issue._description = '\n'.join([desc.text for desc in radar.description.items()])
        issue._opened = False if radar.state in ('Verify', 'Closed') else True
        issue._state = radar.state
        issue._substate = radar.substate
        if radar.duplicateOfProblemID is not None:
            issue._original = self.issue(radar.duplicateOfProblemID)
        issue._creator = self.user(
            name='{} {}'.format(radar.originator.firstName, radar.originator.lastName),
            username=radar.originator.dsid,
            email=radar.originator.email,
        )
        issue._milestone = radar.milestone.name if radar.milestone else ''

        if member == 'source_changes':
            issue._source_changes = []
            if radar.sourceChanges is not None:
                issue._source_changes = radar.sourceChanges.splitlines()

        if member == 'keywords':
            issue._keywords = [kw.name for kw in (radar.keywords() or [])]

        if member == 'classification':
            issue._classification = radar.classification

        if member == 'watchers':
            issue._watchers = []
            for member in radar.cc_memberships.items():
                if member.person.dsid == radar.originator.dsid:
                    continue
                issue._watchers.append(self.user(
                    name='{} {}'.format(member.person.firstName, member.person.lastName),
                    username=member.person.dsid,
                    email=member.person.email,
                ))

        if member == 'comments':
            issue._comments = []
            for item in radar.diagnosis.items(type='user'):
                issue._comments.append(Issue.Comment(
                    user=self.user(
                        name=item.addedBy.name,
                        email=item.addedBy.email,
                    ), timestamp=int(calendar.timegm(item.addedAt.timetuple())),
                    content=item.text,
                ))

        if member == 'references':
            issue._references = []
            refs = set()

            for text in [issue.description] + [comment.content for comment in issue.comments]:
                for match in self.REFERENCE_RE.findall(text):
                    candidate = GenericTracker.from_string(match[0]) or self.from_string(match[0])
                    if not candidate or candidate.link in refs or candidate.id == issue.id:
                        continue
                    issue._references.append(candidate)
                    refs.add(candidate.link)

            for r in radar.related_radars():
                candidate = self.issue(r.id)
                if candidate.link in refs or candidate.id == issue.id:
                    continue
                issue._references.append(candidate)
                refs.add(candidate.link)

        if radar.component and member in ('project', 'component', 'version'):
            issue._project = ''
            issue._component = radar.component.get('name', '')
            issue._version = radar.component.get('version', 'All')
            for project in self._projects:
                if issue._component.startswith(project):
                    issue._project = project
                    issue._component = issue._component[len(project):].lstrip()
                    break

        if member == 'duplicates':
            issue._duplicates = []
            for r in radar.relationships([self.radarclient().Relationship.TYPE_ORIGINAL_OF]):
                if r.related_radar:
                    issue._duplicates.append(self.issue(r.related_radar.id))

        if member == 'related':
            issue._related = {r: [] for r in self.RELATIONSHIP_TYPES}
            for r in radar.relationships():
                issue._related[r.type].append(self.issue(r.related_radar_id))

        return issue

    @handle_access_exception
    def set(self, issue, assignee=None, opened=None, why=None, project=None, component=None, version=None, original=None, keywords=None, source_changes=None, state=None, substate=None, resolution=None, see_also=None, **properties):
        if not self.client or not self.library:
            sys.stderr.write('radarclient inaccessible on this machine\n')
            return None
        if properties:
            raise TypeError("'{}' is an invalid property".format(list(properties.keys())[0]))

        additional_fields = []
        if source_changes:
            additional_fields.append('sourceChanges')
        radar = self.client.radar_for_id(issue.id, additional_fields=additional_fields)
        if not radar:
            sys.stderr.write("Failed to fetch '{}'\n".format(issue.link))
            return None

        did_change = False

        if assignee:
            if not isinstance(assignee, User):
                raise TypeError("Must assign to '{}', not '{}'".format(User, type(assignee)))
            issue._assignee = self.user(name=assignee.name, username=assignee.username, email=assignee.email)
            radar.assignee = self.library.Person({'dsid': int(issue._assignee.username)})
            did_change = True

        if opened is not None:
            issue._opened = bool(opened)
            if issue._opened:
                radar.state = 'Analyze'
                if radar.milestone is None or radar.priority == Priority.NOT_SET:
                    radar.substate = 'Screen'
                else:
                    radar.substate = 'Investigate'
                radar.resolution = 'Unresolved'
            else:
                radar.state = 'Verify'
                if original:
                    radar.resolution = 'Duplicate'
                    radar.duplicateOfProblemID = original.id
                    issue._original = original
                else:
                    radar.resolution = 'Software Changed'
            issue._state = radar.state
            issue._substate = radar.substate
            did_change = True

        if state is not None:
            if radar.state == 'Analyze' and state != 'Analyze':
                radar.resolution = resolution or 'Software Changed'
            radar.state = state
            issue._state = state
            did_change = True

        if substate is not None:
            radar.substate = substate
            issue._substate = substate
            did_change = True

        if project or component or version:
            if not project and len(self.projects) == 1:
                project = list(self.projects.keys())[0]
            if not project:
                raise ValueError('No project provided')
            if not self.projects.get(project):
                raise ValueError("'{}' is not a recognized project".format(project))

            components = self.projects.get(project, {}).get('components', {}).keys()
            if not component and len(components) == 1:
                component = components[0]
            if not component or component == '*':
                component = ''
            if component and component not in components:
                raise ValueError("'{}' is not a recognized component of '{}'".format(component, project))

            if component:
                versions = self.projects.get(project, {}).get('components', {}).get(component, {}).get('versions', [])
            else:
                versions = self.projects.get(project, {}).get('versions', [])
            if not version:
                version = versions[0]
            if version not in versions:
                raise ValueError("'{}' is not a recognized version in '{} {}'".format(version, project, component))

            components = self.client.find_components(dict(
                name=dict(eq='{} {}'.format(project, component)),
                version=dict(eq=version),
            ))
            if not components:
                raise ValueError("No components match '{}' with version '{}'".format('{} {}'.format(project, component), version))
            if len(components) > 1:
                raise ValueError("{} components match '{}' with version '{}'".format(len(components), '{} {}'.format(project, component), version))
            radar.component = components[0]
            did_change = True

            issue._project = project
            issue._component = component
            issue._version = version

        if keywords is not None:
            for keyword in keywords + issue.keywords:
                if keyword not in self._invalid_keywords and keyword not in self._keywords:
                    candidates = self.client.keywords_for_name(keyword)
                    for candidate in candidates:
                        self._keywords[candidate.name] = candidate
                if keyword in self._keywords:
                    continue
                self._invalid_keywords.add(keyword)
                raise ValueError("'{}' is not a valid keyword".format(keyword))

            for word in issue.keywords:
                if word not in keywords:
                    radar.remove_keyword(self._keywords[word])
            for word in keywords:
                if word not in issue.keywords:
                    radar.add_keyword(self._keywords[word])
            did_change = True
            issue._keywords = keywords

        if source_changes:
            did_change = True
            radar.sourceChanges = '\n'.join(source_changes)

        if see_also:
            sys.stderr.write('Radar does not support the see_also field at this time\n')
            return None

        if did_change:
            radar.commit_changes()
        return self.add_comment(issue, why) if why else issue

    @handle_access_exception
    def add_comment(self, issue, text):
        if not self.client or not self.library:
            sys.stderr.write('radarclient inaccessible on this machine\n')
            return None

        radar = self.client.radar_for_id(issue.id)
        if not radar:
            sys.stderr.write("Failed to fetch '{}'\n".format(issue.link))
            return None

        comment = self.library.DiagnosisEntry()
        comment.text = text
        radar.diagnosis.add(comment)
        radar.commit_changes()

        result = Issue.Comment(
            user=self.me(),
            timestamp=int(time.time()),
            content=comment.text,
        )
        if not issue._comments:
            self.populate(issue, 'comments')
        issue._comments.append(result)

        return result

    @handle_access_exception
    def create_relationship(self, issue, issue2, relationship):
        if relationship not in self.RELATIONSHIP_TYPES:
            sys.stderr.write('{} is not a valid relationship type.'.format(relationship))
            return None

        radar = self.client.radar_for_id(issue.id)
        if not radar:
            sys.stderr.write("Failed to fetch '{}'\n".format(issue.link))
            return None

        radar2 = self.client.radar_for_id(issue2.id)
        if not radar2:
            sys.stderr.write("Failed to fetch '{}'\n".format(issue2.link))
            return None

        if relationship == self.radarclient().Relationship.TYPE_DUPLICATE_OF:
            radar.state = 'Verify'
            radar.resolution = 'Duplicate'
            radar.duplicateOfProblemID = issue2.id
            issue._original = issue2
            radar.commit_changes()
        elif relationship == self.radarclient().Relationship.TYPE_ORIGINAL_OF:
            radar2 = self.client.radar_for_id(issue2.id)
            radar2.state = 'Verify'
            radar2.resolution = 'Duplicate'
            radar2.duplicateOfProblemID = issue.id
            issue2._original = issue
            radar2.commit_changes()
        else:
            new_relationship = self.radarclient().Relationship(relationship, radar, radar2)
            radar.add_relationship(new_relationship)
            radar.commit_changes()

        if not issue._related:
            self.populate(issue, 'related')
        else:
            issue._related[relationship].append(issue2)

        return None

    @handle_access_exception
    def relate(self, issue, related_to=None, blocked_by=None, blocking=None, parent_of=None, subtask_of=None,
               cause_of=None, caused_by=None, duplicate_of=None, original_of=None, **relations):
        if relations:
            raise TypeError("'{}' is an invalid relation".format(list(relations.keys())[0]))

        if not self.client or not self.library:
            sys.stderr.write('radarclient inaccessible on this machine\n')
            return None

        try:
            if related_to:
                self.create_relationship(issue, related_to, self.radarclient().Relationship.TYPE_RELATED_TO)
            if blocked_by:
                self.create_relationship(issue, blocked_by, self.radarclient().Relationship.TYPE_BLOCKED_BY)
            if blocking:
                self.create_relationship(issue, blocking, self.radarclient().Relationship.TYPE_BLOCKING)
            if parent_of:
                self.create_relationship(issue, parent_of, self.radarclient().Relationship.TYPE_PARENT_OF)
            if subtask_of:
                self.create_relationship(issue, subtask_of, self.radarclient().Relationship.TYPE_SUBTASK_OF)
            if cause_of:
                self.create_relationship(issue, cause_of, self.radarclient().Relationship.TYPE_CAUSE_OF)
            if caused_by:
                self.create_relationship(issue, caused_by, self.radarclient().Relationship.TYPE_CAUSED_BY)
            if duplicate_of:
                self.create_relationship(issue, duplicate_of, self.radarclient().Relationship.TYPE_DUPLICATE_OF)
            if original_of:
                self.create_relationship(issue, original_of, self.radarclient().Relationship.TYPE_ORIGINAL_OF)
        except AttributeError:
            raise AttributeError('Input should be Issue objects.')

        return issue

    @property
    @webkitcorepy.decorators.Memoize()
    @handle_access_exception
    def projects(self):
        result = dict()
        for project in self._projects:
            result[project] = dict(
                components=dict(),
                description=None,
                versions=[],
            )
            for component in self.client.find_components(dict(name=dict(like=project + '%'), isClosed=False)):
                name = component.name[(len(project)):].lstrip()
                if not name:
                    if 'All' not in result[project]['versions']:
                        result[project]['description'] = component.description
                    result[project]['versions'].append(component.version)
                    continue

                if name not in result[project]['components']:
                    result[project]['components'][name] = dict(description=None, versions=[])
                if 'All' not in result[project]['versions']:
                    result[project]['components'][name]['description'] = component.description
                result[project]['components'][name]['versions'].append(component.version)
        return result

    @handle_access_exception
    def create(
        self, title, description,
        project=None, component=None, version=None,
        classification=None, reproducible=None,
        assign=True, keywords=None,
    ):
        if not title:
            raise ValueError('Must define title to create bug')
        if not description:
            raise ValueError('Must define description to create bug')

        if not project and len(self.projects) == 1:
            project = list(self.projects.keys())[0]
        if not project:
            project = webkitcorepy.Terminal.choose(
                'What project should the bug be associated with?',
                options=sorted(self.projects.keys()), numbered=True,
            )

        components = sorted(self.projects.get(project, {}).get('components', {}).keys())
        if not component and len(components) == 1:
            component = components[0]
        elif not component and components:
            if self.projects[project]['versions']:
                components = ['*'] + components
            component = webkitcorepy.Terminal.choose(
                "What component in '{}' should the bug be associated with?".format(project),
                options=components, numbered=True, default=('*' if components[0] == '*' else None),
            )
        if not component or component == '*':
            component = ''

        if component:
            versions = self.projects.get(project, {}).get('components', {}).get(component, {}).get('versions', [])
        else:
            versions = self.projects.get(project, {}).get('versions', [])
        if not version and len(versions) == 1:
            version = versions[0]
        elif not version and versions:
            version = webkitcorepy.Terminal.choose(
                "What version of '{}{}' should the bug be associated with?".format(project, (' ' + component) if component else ''),
                options=versions, numbered=True, default=('All' if 'All' in versions else None),
            )
        if not version:
            version = 'All'

        # Don't perform any checks of project, component or version. Radar defines many more projects than this class
        # is aware of, if the caller knows better, trust them.

        classification = classification or self.CLASSIFICATIONS[0]
        if classification not in self.CLASSIFICATIONS:
            raise ValueError("'{}' is not a valid bug classification".format(classification))

        reproducible = reproducible or self.REPRODUCIBILITY[0]
        if reproducible not in self.REPRODUCIBILITY:
            raise ValueError("'{}' is not a valid reproducibility argument".format(classification))

        try:
            name = '{} {}'.format(project, component) if component else project
            response = self.client.create_radar(dict(
                title=title,
                description=description,
                component=dict(name=name, version=version),
                classification=classification,
                reproducible=reproducible,
            ))
        except self.library.exceptions.UnsuccessfulResponseException as e:
            sys.stderr.write('Failed to create radar:\n')
            sys.stderr.write('{}\n'.format(e))
            return None

        result = self.issue(response.id)
        if assign:
            result.assign(self.me())
        return result

    def cc_radar(self, issue, block=False, timeout=None, radar=None):
        # cc-ing radar is a no-op for radar
        return issue

    @handle_access_exception
    def clone(
        self, issue, reason,
        project=None, component=None, version=None,
        assign=True,
    ):
        if not reason:
            raise ValueError('Reason must be provided for a clone')
        if (not self.client or not self.library) and member:
            sys.stderr.write('radarclient inaccessible on this machine\n')
            return None

        project = project or issue.project
        component = component or issue.component
        version = version or issue.version

        try:
            name = '{} {}'.format(project, component) if component else project
            clone = self.client.clone_radar(
                issue.id, reason_text=reason,
                component=dict(name=name.strip(), version=version),
            )
        except self.library.exceptions.UnsuccessfulResponseException as e:
            sys.stderr.write('Failed to clone {}:\n'.format(issue))
            sys.stderr.write('{}\n'.format(e))
            return None

        result = self.issue(clone.id)
        if assign:
            result.assign(self.me())
        return result

    @handle_access_exception
    def search(self, query):
        if not query or len(query) == 0:
            raise ValueError('Query must be provided')

        radars = self.client.find_radars(query, return_find_results_directly=True)
        issues = []
        for radar in radars:
            if radar.id:
                issue = Issue(id=radar.id, tracker=self)
                issues.append(issue)

        return issues
