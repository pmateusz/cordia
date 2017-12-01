"""Point of interest at the GPS location"""

import rows.model.object


class PointOfInterest(rows.model.object.DataObject):
    """Point of interest at the GPS location"""

    class Tag(rows.model.object.DataObject):
        """Groups information about the point of interest"""

        KEY = 'key'
        LABEL = 'label'
        DOMAIN = 'domain'

        def __init__(self, **kwargs):
            super(PointOfInterest.Tag, self).__init__()

            self.__key = kwargs.get(PointOfInterest.Tag.KEY, None)
            self.__label = kwargs.get(PointOfInterest.Tag.LABEL, None)
            self.__domain = kwargs.get(PointOfInterest.Tag.DOMAIN, None)

        def as_dict(self):
            bundle = super(PointOfInterest.Tag, self).as_dict()
            if self.__key:
                bundle[PointOfInterest.Tag.KEY] = self.__key
            if self.__label:
                bundle[PointOfInterest.Tag.LABEL] = self.__label
            if self.__domain:
                bundle[PointOfInterest.Tag.DOMAIN] = self.__domain
            return bundle

        @property
        def key(self):
            """Get a property"""

            return self.__key

        @property
        def label(self):
            """Get a property"""

            return self.__label

        @property
        def domain(self):
            """Get a property"""

            return self.__domain

    def __init__(self, **kwargs):
        super(PointOfInterest, self).__init__()

        self.__tags = []

        # handle standard tag
        key, label, domain = kwargs.get('place_id', None), kwargs.get('class', None), kwargs.get('type', None)
        if key or label or domain:
            self.__tags.append(PointOfInterest.Tag(key=key, label=label, domain=domain))

        key, label = kwargs.get('osm_id', None), kwargs.get('osm_type', None)
        # handle Open Street Map tag
        if key or label:
            self.__tags.append(PointOfInterest.Tag(key=key, label=label, domain=None))

    @property
    def tags(self):
        """Return a property"""

        return self.__tags
